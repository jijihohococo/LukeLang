#!/usr/bin/env bash
# Deploy site/ to the lukelang.org host.
#
#   scripts/deploy_site.sh                 # deploy to the default host
#   HOST=root@1.2.3.4 scripts/deploy_site.sh
#
# Uploads into a staging directory and swaps it into place, so a failed
# transfer never leaves a half-written document root. The previous release
# stays at /var/www/lukelang.prev for a one-command rollback:
#
#   ssh $HOST 'rm -rf /var/www/lukelang && mv /var/www/lukelang.prev /var/www/lukelang && systemctl reload nginx'
#
# Authentication is whatever ssh is already configured to use — set up a key
# rather than typing a password into a script.

set -euo pipefail

HOST="${HOST:-root@187.127.108.32}"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
REMOTE_ROOT="${REMOTE_ROOT:-/var/www/lukelang}"
BUNDLE="$(mktemp -t lukelang-site-XXXX.tgz)"
trap 'rm -f "$BUNDLE"' EXIT

echo "→ regenerating hosted documentation"
python3 "$ROOT_DIR/scripts/build_site_docs.py"

echo "→ packaging site/"
tar -czf "$BUNDLE" -C "$ROOT_DIR/site" --exclude README.md .
echo "  $(du -h "$BUNDLE" | cut -f1)"

echo "→ uploading to $HOST"
scp -q "$BUNDLE" "$HOST:/tmp/lukelang-site.tgz"

echo "→ swapping release"
ssh "$HOST" 'bash -s' <<REMOTE
set -euo pipefail
rm -rf "$REMOTE_ROOT.new"
mkdir -p "$REMOTE_ROOT.new"
tar -xzf /tmp/lukelang-site.tgz -C "$REMOTE_ROOT.new"
rm -f /tmp/lukelang-site.tgz

if [ -d "$REMOTE_ROOT" ]; then
  rm -rf "$REMOTE_ROOT.prev"
  mv "$REMOTE_ROOT" "$REMOTE_ROOT.prev"
fi
mv "$REMOTE_ROOT.new" "$REMOTE_ROOT"

chown -R root:root "$REMOTE_ROOT"
find "$REMOTE_ROOT" -type d -exec chmod 755 {} +
find "$REMOTE_ROOT" -type f -exec chmod 644 {} +

nginx -t
systemctl reload nginx
echo "  \$(find "$REMOTE_ROOT" -type f | wc -l) files live"
REMOTE

echo "→ verifying"
IP="${HOST##*@}"
for path in / /learn/ /docs/ /download/ /examples/ /community/ /news/; do
  code=$(curl -sS -o /dev/null -w '%{http_code}' -H 'Host: lukelang.org' "http://$IP$path")
  printf '  %s %s\n' "$code" "$path"
done
code=$(curl -sS -o /dev/null -w '%{http_code}' -H 'Host: status.lukelang.org' "http://$IP/")
printf '  %s %s\n' "$code" "status.lukelang.org/"

echo "done"
