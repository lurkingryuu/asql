#!/bin/bash

set -euo pipefail

MODE="${1:-full}"
shift || true

SOURCE_DIR="${SOURCE_DIR:-/mysql-source}"
BUILD_DIR="${BUILD_DIR:-/mysql-build}"
RUNTIME_DIR="${BUILD_DIR}/runtime_output_directory"
PLUGIN_DIR="${BUILD_DIR}/plugin_output_directory"
MYSQLD_BIN="${MYSQLD_BIN:-${RUNTIME_DIR}/mysqld}"
MYSQL_BIN="${MYSQL_BIN:-${RUNTIME_DIR}/mysql}"
MYSQL_SOCKET="${MYSQL_SOCKET:-/tmp/asql-unittest.sock}"
MYSQL_PORT="${MYSQL_PORT:-3307}"
MYSQL_DATADIR="${MYSQL_DATADIR:-/tmp/asql-unittest-data}"
MYSQL_PIDFILE="${MYSQL_PIDFILE:-/tmp/asql-unittest.pid}"
MYSQL_LOGFILE="${MYSQL_LOGFILE:-/tmp/asql-unittest.log}"
SYNC_SERVICE_PORT="${SYNC_SERVICE_PORT:-18180}"
SYNC_SERVICE_PID=""

cleanup() {
  if [ -n "${SYNC_SERVICE_PID}" ] && kill -0 "${SYNC_SERVICE_PID}" >/dev/null 2>&1; then
    kill "${SYNC_SERVICE_PID}" >/dev/null 2>&1 || true
    wait "${SYNC_SERVICE_PID}" >/dev/null 2>&1 || true
  fi

  if [ -f "${MYSQL_PIDFILE}" ]; then
    local pid
    pid="$(tr -d '[:space:]' < "${MYSQL_PIDFILE}")"
    if [ -n "${pid}" ] && kill -0 "${pid}" >/dev/null 2>&1; then
      "${MYSQL_BIN}" --protocol=SOCKET --socket="${MYSQL_SOCKET}" -u root \
        -e "SHUTDOWN;" >/dev/null 2>&1 || kill "${pid}" >/dev/null 2>&1 || true
      wait "${pid}" >/dev/null 2>&1 || true
    fi
  fi
}
trap cleanup EXIT

mysql_exec() {
  "${MYSQL_BIN}" --protocol=SOCKET --socket="${MYSQL_SOCKET}" -u root -N -B -e "$1"
}

mysql_exec_as_user() {
  local user="$1"
  local pass="$2"
  local query="$3"
  "${MYSQL_BIN}" --protocol=SOCKET --socket="${MYSQL_SOCKET}" -u "${user}" \
    -p"${pass}" -N -B -e "${query}"
}

expect_success() {
  local desc="$1"
  local user="$2"
  local pass="$3"
  local query="$4"
  if mysql_exec_as_user "${user}" "${pass}" "${query}" >/dev/null 2>&1; then
    echo "[PASS] ${desc}"
  else
    echo "[FAIL] ${desc}" >&2
    return 1
  fi
}

expect_failure() {
  local desc="$1"
  local user="$2"
  local pass="$3"
  local query="$4"
  if mysql_exec_as_user "${user}" "${pass}" "${query}" >/tmp/asql-unittest.err 2>&1; then
    echo "[FAIL] ${desc}" >&2
    return 1
  fi
  echo "[PASS] ${desc}"
}

status_value() {
  mysql_exec "SHOW GLOBAL STATUS LIKE '$1';" | awk 'NR==1 {print $2}'
}

start_mysql() {
  rm -rf "${MYSQL_DATADIR}"
  mkdir -p "${MYSQL_DATADIR}"
  rm -f "${MYSQL_SOCKET}" "${MYSQL_PIDFILE}" "${MYSQL_LOGFILE}"

  "${MYSQLD_BIN}" --no-defaults --initialize-insecure \
    --datadir="${MYSQL_DATADIR}" \
    --log-error="${MYSQL_LOGFILE}"

  "${MYSQLD_BIN}" --no-defaults \
    --datadir="${MYSQL_DATADIR}" \
    --socket="${MYSQL_SOCKET}" \
    --port="${MYSQL_PORT}" \
    --pid-file="${MYSQL_PIDFILE}" \
    --log-error="${MYSQL_LOGFILE}" \
    --plugin-dir="${PLUGIN_DIR}" \
    --daemonize

  for _ in $(seq 1 60); do
    if "${MYSQL_BIN}" --protocol=SOCKET --socket="${MYSQL_SOCKET}" -u root \
      -e "SELECT 1" >/dev/null 2>&1; then
      return 0
    fi
    sleep 1
  done

  echo "MySQL failed to start; see ${MYSQL_LOGFILE}" >&2
  return 1
}

run_unit_tests() {
  ctest --test-dir "${BUILD_DIR}" --output-on-failure -R 'authorization-t|embedded-cedar-t'
}

run_embedded_integration() {
  local fixture_dir="${SOURCE_DIR}/plugin/authorization/testdata/embedded_cedar"

  mysql_exec "INSTALL PLUGIN embedded_cedar SONAME 'embedded_cedar.so';" >/dev/null 2>&1 || true
  mysql_exec "DROP DATABASE IF EXISTS test_cedar_db;"
  mysql_exec "CREATE DATABASE test_cedar_db;"
  mysql_exec "USE test_cedar_db; CREATE TABLE employees (id INT PRIMARY KEY, name VARCHAR(100), department VARCHAR(50));"
  mysql_exec "USE test_cedar_db; CREATE TABLE payroll (employee_id INT PRIMARY KEY, base_salary INT, tax_deductions INT);"
  mysql_exec "USE test_cedar_db; CREATE TABLE public_data (id INT PRIMARY KEY, title VARCHAR(100));"
  mysql_exec "USE test_cedar_db; INSERT INTO employees VALUES (1, 'Alice', 'Engineering'), (2, 'Bob', 'Sales');"
  mysql_exec "USE test_cedar_db; INSERT INTO payroll VALUES (1, 100, 20), (2, 120, 25);"
  mysql_exec "USE test_cedar_db; INSERT INTO public_data VALUES (1, 'public');"
  "${MYSQL_BIN}" --protocol=SOCKET --socket="${MYSQL_SOCKET}" -u root <<'SQL'
USE test_cedar_db;
DROP PROCEDURE IF EXISTS calculate_bonus;
DELIMITER //
CREATE PROCEDURE calculate_bonus(IN emp_id INT)
BEGIN
  SELECT base_salary FROM payroll WHERE employee_id = emp_id;
END //
DELIMITER ;
SQL

  for user in alice bob developer hr_user; do
    mysql_exec "DROP USER IF EXISTS '${user}'@'localhost';"
    mysql_exec "CREATE USER '${user}'@'localhost' IDENTIFIED BY 'password123';"
  done

  mysql_exec "SET GLOBAL embedded_cedar_policy_file = '${fixture_dir}/basic_policy_v1.cedar';"
  mysql_exec "SET GLOBAL embedded_cedar_schema_file = '${fixture_dir}/schema.json';"
  mysql_exec "SET GLOBAL embedded_cedar_entities_file = '${fixture_dir}/entities.json';"
  mysql_exec "SET GLOBAL embedded_cedar_namespace = 'MySQL';"
  mysql_exec "SET GLOBAL embedded_cedar_enabled = ON;"
  mysql_exec "SET GLOBAL embedded_cedar_collect_stats = ON;"
  mysql_exec "SET GLOBAL embedded_cedar_cache_enabled = ON;"
  mysql_exec "SET GLOBAL embedded_cedar_cache_ttl = 300;"
  mysql_exec "SET GLOBAL embedded_cedar_enable_column_access = OFF;"
  mysql_exec "SET GLOBAL embedded_cedar_reset_stats = ON;"
  mysql_exec "SET GLOBAL embedded_cedar_reload = ON;"

  expect_success "db access via USE for alice" \
    "alice" "password123" "USE test_cedar_db; SHOW TABLES LIKE 'employees';"
  expect_failure "db access denied for bob" \
    "bob" "password123" "USE test_cedar_db; SHOW TABLES LIKE 'employees';"
  expect_success "table select allowed for alice" \
    "alice" "password123" "SELECT * FROM test_cedar_db.employees LIMIT 1;"
  expect_failure "table select denied for bob" \
    "bob" "password123" "SELECT * FROM test_cedar_db.employees LIMIT 1;"
  expect_success "routine execute allowed for developer" \
    "developer" "password123" "CALL test_cedar_db.calculate_bonus(1);"
  expect_failure "routine execute denied for bob" \
    "bob" "password123" "CALL test_cedar_db.calculate_bonus(1);"

  mysql_exec "SET GLOBAL embedded_cedar_enable_column_access = ON;"
  expect_success "column select allowed for hr_user" \
    "hr_user" "password123" "SELECT base_salary FROM test_cedar_db.payroll LIMIT 1;"
  expect_failure "column select denied for hr_user without policy" \
    "hr_user" "password123" "SELECT tax_deductions FROM test_cedar_db.payroll LIMIT 1;"

  expect_success "cache warmup for alice" \
    "alice" "password123" "SELECT * FROM test_cedar_db.employees LIMIT 1;"

  mysql_exec "SET GLOBAL embedded_cedar_policy_file = '${fixture_dir}/basic_policy_v2.cedar';"
  mysql_exec "SET GLOBAL embedded_cedar_reload = ON;"

  expect_failure "reload revoked alice access" \
    "alice" "password123" "SELECT * FROM test_cedar_db.employees LIMIT 1;"
  expect_success "reload granted bob access" \
    "bob" "password123" "SELECT * FROM test_cedar_db.employees LIMIT 1;"
  expect_success "cache hit for bob after reload" \
    "bob" "password123" "SELECT * FROM test_cedar_db.employees LIMIT 1;"

  local requests grants denies errors hits misses
  requests="$(status_value embedded_cedar_requests)"
  grants="$(status_value embedded_cedar_grants)"
  denies="$(status_value embedded_cedar_denies)"
  errors="$(status_value embedded_cedar_errors)"
  hits="$(status_value embedded_cedar_cache_hits)"
  misses="$(status_value embedded_cedar_cache_misses)"

  test "${requests}" -gt 0
  test "${grants}" -gt 0
  test "${denies}" -gt 0
  test "${errors}" -eq 0
  test "${hits}" -gt 0
  test "${misses}" -gt 0
  echo "[PASS] embedded status vars look sane"

  mysql_exec "UNINSTALL PLUGIN embedded_cedar;" >/dev/null 2>&1 || true
  for user in alice bob developer hr_user; do
    mysql_exec "DROP USER IF EXISTS '${user}'@'localhost';"
  done
  mysql_exec "DROP DATABASE IF EXISTS test_cedar_db;"
}

start_sync_service() {
  python3 "${SOURCE_DIR}/unittest/docker/mock_cedar_sync_service.py" \
    --host 127.0.0.1 --port "${SYNC_SERVICE_PORT}" >/tmp/mock-cedar-sync.log 2>&1 &
  SYNC_SERVICE_PID=$!
  for _ in $(seq 1 30); do
    if curl -fsS "http://127.0.0.1:${SYNC_SERVICE_PORT}/health" >/dev/null 2>&1; then
      return 0
    fi
    sleep 1
  done
  echo "mock sync service failed to start" >&2
  return 1
}

run_integrated_embedded() {
  start_sync_service
  "${SOURCE_DIR}/test_integrated_plugins.sh" \
    --mysql-socket "${MYSQL_SOCKET}" \
    --authorization-mode embedded \
    --cedar-url "http://127.0.0.1:${SYNC_SERVICE_PORT}/v1" \
    --embedded-policy-file "${SOURCE_DIR}/plugin/authorization/testdata/embedded_cedar/integrated_policy.cedar" \
    --embedded-schema-file "${SOURCE_DIR}/plugin/authorization/testdata/embedded_cedar/schema.json" \
    --embedded-entities-file "${SOURCE_DIR}/plugin/authorization/testdata/embedded_cedar/entities.json"
}

case "${MODE}" in
  unit)
    run_unit_tests
    ;;
  embedded-integration)
    start_mysql
    run_embedded_integration
    ;;
  integrated-e2e)
    start_mysql
    run_integrated_embedded
    ;;
  full)
    run_unit_tests
    start_mysql
    run_embedded_integration
    run_integrated_embedded
    ;;
  *)
    echo "unknown mode: ${MODE}" >&2
    exit 1
    ;;
esac
