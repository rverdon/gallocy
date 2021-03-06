#!/usr/bin/env bats


setup() {
  echo "SETUP"
  BIN=install/bin
}


teardown() {
  echo "TEARDOWN"
}


@test "http server binary exists" {
  run ls "${BIN}/server"
  [ $status -eq 0 ]
}


@test "http server starts" {
  ${BIN}/server &
  PID=$!
  run kill -s 0 $PID
  kill -9 $PID
  [ "$status" -eq 0 ]
}


@test "http server responds on /admin" {
  ${BIN}/server &
  PID=$!
  echo "Started server on ${PID}"
  run curl --max-time 1 http://localhost:8080/admin
  echo "RESULTS $status $result"
  kill -9 $PID
  [ "$status" -eq 0 ]
}
