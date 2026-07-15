#!/usr/bin/env bash
set -u

if grep -q '^PASS$' "$1"; then
  echo 'FITNESS pass=1/1 metric=1'
else
  echo 'FITNESS pass=0/1 metric=0'
fi
