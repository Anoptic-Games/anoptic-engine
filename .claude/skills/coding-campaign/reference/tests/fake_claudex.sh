#!/usr/bin/env bash
set -u

name=${CAMPAIGN_CANDIDATE_PATH##*/}
idx=${name#cand_}
idx=${idx%%.*}

case "${FAKE_SCENARIO:-deadline}:$idx" in
  deadline:01) printf 'PASS\n';;
  deadline:02) printf 'FAIL\n';;
  deadline:*) sleep 8; printf 'PASS\n';;
  luna-trigger:05) printf 'PASS\n';;
  luna-trigger:*) printf 'FAIL\n';;
  all-pass:*) printf 'PASS\n';;
  *) exit 3;;
esac
