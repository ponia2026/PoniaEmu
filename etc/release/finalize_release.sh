#!/bin/bash
# After the Release workflow succeeded: add the Linux packages, rebuild SHA256SUMS.txt, set the notes, then verify.
# Usage: finalize_release.sh <run-id> <commit8>
set -u
R=ponia2026/PoniaEmu; TAG=v1.0.0
RUN=${1:?run id}; C=${2:?commit}
W=$HOME/work/poniaemu-release
D=$W/dist
BT=$HOME/android/sdk/build-tools/33.0.2

gh run watch "$RUN" --repo $R --interval 45 --exit-status >/dev/null 2>&1
rc=$?
echo "workflow run rc=$rc"
gh run view "$RUN" --repo $R --json conclusion,jobs -q '"conclusion=" + .conclusion, (.jobs[] | "  " + .name + ": " + .conclusion)'
if [ $rc -ne 0 ]; then
    gh run view "$RUN" --repo $R --json jobs -q '.jobs[] | .name as $n | .steps[] | select(.conclusion=="failure") | "FAILED " + $n + ": " + .name'
    echo "NOT publishing anything else."; exit 1
fi

T=$(mktemp -d)
# The notes come from the template in the repository (etc/release), filled in for this tag and stamp; it also checks that every file name they mention exists.
STAMP=$(basename "$(ls "$D"/*"$C"*_linux-x86_64.tar.gz | head -1)" | sed -e "s/^PoniaEmu_[^-]*-//" -e "s/_linux.*//")
sh "$W/src/etc/release/make_notes.sh" "$TAG" "$STAMP" > "$W/release-notes-$TAG.md" || { echo "cannot make the release notes"; exit 1; }
gh release download $TAG --repo $R -p SHA256SUMS.txt -D "$T" || { echo "cannot get SHA256SUMS.txt"; exit 1; }
( cd "$D" && sha256sum ./*"$C"*.tar.gz | sed 's# \./# #' ) >> "$T/SHA256SUMS.txt"
echo "--- SHA256SUMS.txt will contain:"; cat "$T/SHA256SUMS.txt"
gh release upload $TAG --repo $R "$D"/*"$C"*.tar.gz "$T/SHA256SUMS.txt" --clobber || { echo "upload failed"; exit 1; }
gh release edit $TAG --repo $R --title "PoniaEmu ${TAG#v}" --notes-file "$W/release-notes-$TAG.md" || echo "notes edit failed"

echo; echo "=================== VERIFY"
gh release view $TAG --repo $R --json url,targetCommitish,isDraft,isPrerelease,assets -q '.url, "target=" + .targetCommitish + " draft=" + (.isDraft|tostring) + " prerelease=" + (.isPrerelease|tostring), (.assets[] | "  " + .name + "  " + (.size|tostring))'
V=$(mktemp -d); cd "$V"
gh release download $TAG --repo $R -D "$V" >/dev/null 2>&1
echo "--- sha256 check of the downloaded files:"; sha256sum -c SHA256SUMS.txt 2>&1
echo "--- APK signing certificates:"
for f in *.apk; do echo "$f"; "$BT/apksigner" verify --print-certs "$f" 2>&1 | grep -E "SHA-256|DN:" | head -2; done
echo "--- player library inside each APK:"
for f in *.apk; do
    p=$(unzip -l "$f" | grep -o "lib/[a-z0-9-]*/libponia_player\.so" | head -1)
    if [ -n "$p" ]; then echo "  ok   $f has $p"; else echo "  MISSING libponia_player.so in $f"; fi
done
echo "--- names used in the notes all exist as assets?"
grep -o 'PoniaEmu_v[^` ]*\.\(apk\|tar\.gz\)' "$W/release-notes-$TAG.md" | sort -u | while read -r n; do [ -f "$V/$n" ] && echo "  ok   $n" || echo "  MISSING $n"; done
echo "--- installer lookup (what install.sh will find):"
for a in x86_64 arm64 armhf; do printf "  %s: " $a; curl -fsSL "https://api.github.com/repos/$R/releases/latest" | grep -o "https://[^\"]*_linux-$a\.tar\.gz" | head -1; done
echo "--- tarball contents (x86_64):"; tar -tzf "$V"/*linux-x86_64.tar.gz | grep -E "bin/|\.desktop" | head -5
rm -rf "$T" "$V"
echo "=================== finalize done"
