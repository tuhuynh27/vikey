#!/bin/zsh
# Proofread eval: the keyboard's AIAssist.swift on this Mac's Apple Intelligence (macOS 26, Apple Intelligence on).
# Not on CI (CI Macs can't run the model).   Tools/proofread/run.sh [label]
set -e
cd "$(dirname "$0")"
OUT=${TMPDIR:-/tmp}/vikey_proofread; mkdir -p $OUT
swiftc -O -parse-as-library -target arm64-apple-macos26 -o $OUT/run_eval run_eval.swift ../../iOS/Keyboard/AIAssist.swift
$OUT/run_eval eval.jsonl $OUT/results.jsonl
python3 score.py eval.jsonl $OUT/results.jsonl ${1:+--label "$1"}
