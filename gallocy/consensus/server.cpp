#include <ctime>
#include <functional>
#include <map>
#include <string>
#include <utility>

#include "gallocy/consensus/server.h"
#include "gallocy/logging.h"
#include "gallocy/models.h"
#include "gallocy/request.h"
#include "gallocy/stringutils.h"
#include "gallocy/threads.h"


/**
 * Die.
 *
 * :param sc: A custom error to pass alont to ``perror``.
 */
void error_die(const char *sc) {
  perror(sc);
  exit(1);
}


Response *GallocyServer::route_admin(RouteArguments *args, Request *request) {
  Response *response = new (internal_malloc(sizeof(Response))) Response();
  response->status_code = 200;
  response->headers["Server"] = "Gallocy-Httpd";
  response->headers["Content-Type"] = "application/json";

  gallocy::json j = {
    {"status", "GOOD" },
    {"master", is_master},
    {"peers", { } },
    {"diagnostics", {
      // TODO(sholsapp): These names suck.. we should indicate that they are allocators.
      { "local_internal_memory", reinterpret_cast<uint64_t>(&local_internal_memory) },
      { "shared_page_table", reinterpret_cast<uint64_t>(&shared_page_table) },
      // TODO(sholsapp): Add the main application allocator here.
    } },
  };

  for (auto p : peer_info_table.all()) {
    gallocy::json peer_info = {
      {"id", p.id},
      {"ip_address", p.ip_address},
      {"first_seen", p.first_seen},
      {"last_seen", p.last_seen},
    };
    j["peers"].push_back(peer_info);
  }

  response->body = j.dump();

  args->~RouteArguments();
  internal_free(args);

  return response;
}


Response *GallocyServer::route_join(RouteArguments *args, Request *request) {
  Response *response = new (internal_malloc(sizeof(Response))) Response();
  // TODO(sholsapp): Setup the joining logic here.

  gallocy::json peer_info = request->get_json();

  // TODO(sholsapp): We need a serialization framework so that we're not doing
  // random JSON building/parsing. Maybe the models should have a codec build
  // into them?
  int ret = e.execute(
    PeerInfo(
      utils::parse_internet_address(peer_info["ip_address"]),
      std::time(nullptr),
      std::time(nullptr),
      static_cast<gallocy::json::boolean_t>(peer_info["is_master"])).insert());

  response->headers["Server"] = "Gallocy-Httpd";
  response->headers["Content-Type"] = "application/json";

  if (ret) {
    response->status_code = 200;
    gallocy::json j = {
      {"status", "JOINED" },
    };
    response->body = j.dump();
  } else {
    response->status_code = 500;
    response->body = "";
  }

  args->~RouteArguments();
  internal_free(args);

  return response;
}


Response *GallocyServer::route_request_vote(RouteArguments *args, Request *request) {
  Response *response = new (internal_malloc(sizeof(Response))) Response();
  response->headers["Server"] = "Gallocy-Httpd";
  response->status_code = 200;
  args->~RouteArguments();
  internal_free(args);
  return response;
}


Response *GallocyServer::route_append_entries(RouteArguments *args, Request *request) {
  Response *response = new (internal_malloc(sizeof(Response))) Response();
  response->headers["Server"] = "Gallocy-Httpd";
  response->status_code = 200;
  args->~RouteArguments();
  internal_free(args);
  return response;
}


void *GallocyServer::work() {
  LOG_INFO("Starting HTTP server on " << address << ":" << port);

  struct sockaddr_in name;
  int optval = 1;

  server_socket = socket(PF_INET, SOCK_STREAM, 0);
  if (server_socket == -1) {
    error_die("socket");
  }

#ifdef __APPLE__
  setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
#else
  setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
#endif

  memset(&name, 0, sizeof(name));
  name.sin_family = AF_INET;
  name.sin_port = htons(port);
  // name.sin_addr.s_addr = htonl(INADDR_ANY);
  name.sin_addr.s_addr = inet_addr(address.c_str());

  if (bind(server_socket, (struct sockaddr *) &name, sizeof(name)) < 0) {
    error_die("bind");
  }

  if (listen(server_socket, 5) < 0) {
    error_die("listen");
  }

  int64_t client_sock = -1;
  struct sockaddr_in client_name;
  uint64_t client_name_len = sizeof(client_name);
  pthread_t newthread;

  while (alive) {
    client_sock = accept(server_socket,
        reinterpret_cast<struct sockaddr *>(&client_name),
        reinterpret_cast<socklen_t *>(&client_name_len));

    if (client_sock == -1) {
      error_die("accept");
    }

    struct RequestContext *ctx =
      new (internal_malloc(sizeof(struct RequestContext))) struct RequestContext;
    ctx->server = this;
    ctx->client_socket = client_sock;
    ctx->client_name = client_name;

    if (__gallocy_pthread_create(&newthread, NULL, handle_entry, reinterpret_cast<void *>(ctx)) != 0) {
      perror("pthread_create1");
    }

    // TODO(sholsapp): This shouldn't block, and we shouldn't just try to
    // join this thread.
    if (__gallocy_pthread_join(newthread, nullptr)) {
      perror("pthread_join1");
    }
  }

  close(server_socket);

  return nullptr;
}


void *GallocyServer::handle_entry(void *arg) {
  struct RequestContext *ctx = reinterpret_cast<struct RequestContext *>(arg);
  void *ret = ctx->server->handle(ctx->client_socket, ctx->client_name);
  ctx->~RequestContext();
  internal_free(ctx);
  return ret;
}


void *GallocyServer::handle(int client_socket, struct sockaddr_in client_name) {
  Request *request = get_request(client_socket);
  Response *response = routes.match(request->uri)(request);
  if (send(client_socket, response->str().c_str(), response->size(), 0) == -1) {
    error_die("send");
  }

  LOG_INFO(request->method
    << " "
    << request->uri
    << " - "
    << "HTTP " << response->status_code
    << " - "
    << inet_ntoa(client_name.sin_addr) << " "
    << request->headers["User-Agent"]);

  // Teardown
  request->~Request();
  internal_free(request);
  response->~Response();
  internal_free(response);

  shutdown(client_socket, SHUT_RDWR);
  close(client_socket);

  return nullptr;
}


Request *GallocyServer::get_request(int client_socket) {
  gallocy::stringstream request;
  int n;
  char buf[256] = {0};
  n = recv(client_socket, buf, 16, 0);
  request << buf;
  while (n > 0) {
    memset(buf, 0, 256);
    n = recv(client_socket, buf, 256, MSG_DONTWAIT);
    request << buf;
  }
  return new (internal_malloc(sizeof(Request))) Request(request.str());
}