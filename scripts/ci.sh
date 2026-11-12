#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
#
# CI: Zephyr compliance checks that apply to this repo, then the twister tests
# on ignuvi (build only) and native_sim/native/ignuvi (built and run).
#
#   scripts/ci.sh [commit range]    default HEAD~1..HEAD
#   HW=1 scripts/ci.sh              also run them on the board (ST-LINK VCP)
#
# Needs the west workspace (../zephyr) and its Python env activated.
set -u
cd "$(dirname "$0")/.."
WS=$(realpath ..)
export ZEPHYR_BASE=$WS/zephyr
OUT=${CI_OUT:-$WS/ci-out}
mkdir -p "$OUT"
rc=0

# Not applicable here:
#   Gitlint: commits use the "[TAG] summary" style
#   BoardYml: the board vendor is the generic "vendor", not a Zephyr prefix
#   Kconfig: app Kconfig symbols (apps/*/Kconfig) are invisible to it
#   SysbuildKconfig: crashes when no SB_CONFIG_ is used (no sysbuild here)
#   LicenseAndCopyrightCheck: upstream policy (copyright line in every file)
#   DeviceMmioCheck: disabled upstream; MaintainersFormat, ModulesMaintainers:
#   Zephyr's MAINTAINERS.yml
python3 "$ZEPHYR_BASE/scripts/ci/check_compliance.py" -c "${1:-HEAD~1..HEAD}" \
	-o "$OUT/compliance.xml" \
	-e Gitlint -e BoardYml -e Kconfig -e SysbuildKconfig -e LicenseAndCopyrightCheck \
	-e DeviceMmioCheck -e MaintainersFormat -e ModulesMaintainers || rc=1
# It writes one <check>.txt per failed check in the repo root
for c in $(python3 "$ZEPHYR_BASE/scripts/ci/check_compliance.py" -l); do
	[ -f "$c.txt" ] && mv "$c.txt" "$OUT/"
done

# The AP6212 patchram is not committed: skip the Bluetooth scenario without it
[ -f apps/bluetooth/bcm43438a0.hcd ] || skip=(--exclude-tag hcd)
hw=()
[ "${HW:-}" ] && hw=(--device-testing --device-serial /dev/ttyACM0
	--device-serial-baud 2000000 --retry-failed 2 -j 1)
(cd "$WS" && west twister -T ignuvi -p ignuvi -p native_sim/native/ignuvi \
	-O "$OUT/twister" --inline-logs ${skip[@]+"${skip[@]}"} ${hw[@]+"${hw[@]}"}) || rc=1

exit $rc
