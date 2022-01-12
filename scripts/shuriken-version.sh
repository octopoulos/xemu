#!/bin/sh

set -eu

dir="$1"
SHURIKEN_DATE=$(date -u)
SHURIKEN_COMMIT=$( \
  cd "$dir"; \
  if test -e .git; then \
    git rev-parse HEAD 2>/dev/null | tr -d '\n'; \
  elif test -e SHURIKEN_COMMIT; then \
    cat SHURIKEN_COMMIT; \
  fi)
SHURIKEN_BRANCH=$( \
  cd "$dir"; \
  if test -e .git; then \
    git symbolic-ref --short HEAD || echo $SHURIKEN_COMMIT; \
  elif test -e SHURIKEN_BRANCH; then \
    cat SHURIKEN_BRANCH; \
  fi)
SHURIKEN_VERSION=$( \
  cd "$dir"; \
  if test -e .git; then \
    git describe --match 'shuriken-v*' | cut -c 7- | tr -d '\n'; \
  elif test -e SHURIKEN_VERSION; then \
    cat SHURIKEN_VERSION; \
  fi)

SHURIKEN_VERSION="0.6.2-34-g8a5fec011e"

get_version_field() {
  echo ${SHURIKEN_VERSION}-0 | cut -d- -f$1
}

get_version_dot () {
  echo $(get_version_field 1) | cut -d. -f$1
}

SHURIKEN_VERSION_MAJOR=$(get_version_dot 1)
SHURIKEN_VERSION_MINOR=$(get_version_dot 2)
SHURIKEN_VERSION_MICRO=$(get_version_dot 3)
SHURIKEN_VERSION_PATCH=$(get_version_field 2)

cat <<EOF
#define SHURIKEN_VERSION       "$SHURIKEN_VERSION"
#define SHURIKEN_VERSION_MAJOR $SHURIKEN_VERSION_MAJOR
#define SHURIKEN_VERSION_MINOR $SHURIKEN_VERSION_MINOR
#define SHURIKEN_VERSION_MICRO $SHURIKEN_VERSION_MICRO
#define SHURIKEN_VERSION_PATCH $SHURIKEN_VERSION_PATCH
#define SHURIKEN_BRANCH        "$SHURIKEN_BRANCH"
#define SHURIKEN_COMMIT        "$SHURIKEN_COMMIT"
#define SHURIKEN_DATE          "$SHURIKEN_DATE"
EOF
