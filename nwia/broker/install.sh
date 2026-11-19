#!/usr/bin/env bash
# Run on a NEW Debian/Ubuntu broker server after reviewing this file.
# Usage: sudo bash install.sh mqtt.example.com you@example.com gateway-a=MAC gateway-b=MAC
set -euo pipefail
if [[ $EUID -ne 0 || $# -lt 3 ]]; then
  echo 'Usage: sudo bash install.sh DOMAIN CERTBOT_EMAIL USER=12_HEX_MAC [USER=12_HEX_MAC ...]' >&2
  exit 2
fi
broker_domain=$1
certbot_email=$2
shift 2
[[ $broker_domain =~ ^[A-Za-z0-9.-]+$ && $broker_domain == *.* ]] || { echo 'Invalid DNS hostname' >&2; exit 2; }
[[ $certbot_email == *@* ]] || { echo 'Supply an email address for certificate registration' >&2; exit 2; }
. /etc/os-release
case "${ID:-}" in debian|ubuntu) ;; *) echo 'This installer supports Debian/Ubuntu only' >&2; exit 2;; esac
for item in "$@"; do
  [[ $item =~ ^[a-zA-Z0-9_-]+=([0-9a-fA-F]{12})$ ]] || { echo 'Use username=12_hex_MAC for every gateway' >&2; exit 2; }
done
for path in /etc/mosquitto/conf.d/nwia.conf /etc/mosquitto/nwia.passwd /etc/mosquitto/nwia.acl /root/nwia-credentials.txt; do
  [[ ! -e $path ]] || { echo "Refusing to overwrite $path" >&2; exit 1; }
done
if ss -lnt | awk '{print $4}' | grep -Eq '(:80|:8883)$'; then
  echo 'Port 80 or 8883 already has a listener. Review service placement first.' >&2; exit 1
fi
echo 'DNS must point this hostname at this server. Allow inbound TCP 80 for certificate renewal and TCP 8883 for MQTT.'
getent ahostsv4 "$broker_domain"
read -r -p "Proceed with package installation and interactive certificate registration? [yes/no] " answer
[[ $answer == yes ]] || exit 1
apt-get update
apt-get install -y mosquitto mosquitto-clients certbot openssl
# Certbot displays its own account/terms prompts; do not silently agree to terms.
certbot certonly --standalone -d "$broker_domain" --email "$certbot_email"
install -d -m 0750 -o root -g mosquitto /etc/mosquitto/certs
install -m 0640 -o root -g mosquitto "/etc/letsencrypt/live/$broker_domain/fullchain.pem" /etc/mosquitto/certs/fullchain.pem
install -m 0640 -o root -g mosquitto "/etc/letsencrypt/live/$broker_domain/privkey.pem" /etc/mosquitto/certs/privkey.pem
umask 077
: > /root/nwia-credentials.txt
: > /etc/mosquitto/nwia.acl
first=1
for item in "$@"; do
  user=${item%%=*}; mac=${item#*=}; mac=${mac,,}
  password=$(openssl rand -hex 24)
  if [[ $first == 1 ]]; then mosquitto_passwd -b -c /etc/mosquitto/nwia.passwd "$user" "$password"; first=0
  else mosquitto_passwd -b /etc/mosquitto/nwia.passwd "$user" "$password"; fi
  printf 'user %s\ntopic write nwiamesh/v1/pilot/packets/%s\ntopic read nwiamesh/v1/pilot/packets/+\n\n' "$user" "$mac" >> /etc/mosquitto/nwia.acl
  printf 'Gateway: %s\nHost: %s\nPort: 8883\nGroup: pilot\nPassword: %s\n\n' "$user" "$broker_domain" "$password" >> /root/nwia-credentials.txt
done
chown mosquitto:mosquitto /etc/mosquitto/nwia.passwd /etc/mosquitto/nwia.acl
chmod 0640 /etc/mosquitto/nwia.passwd /etc/mosquitto/nwia.acl
cat > /etc/mosquitto/conf.d/nwia.conf <<'CONF'
listener 8883
protocol mqtt
allow_anonymous false
password_file /etc/mosquitto/nwia.passwd
acl_file /etc/mosquitto/nwia.acl
certfile /etc/mosquitto/certs/fullchain.pem
keyfile /etc/mosquitto/certs/privkey.pem
tls_version tlsv1.2
retain_available false
max_packet_size 512
max_inflight_messages 4
max_queued_messages 4
CONF
# Reload runs after Mosquitto drops root; its config must remain readable.
chown root:mosquitto /etc/mosquitto/conf.d/nwia.conf
chmod 0640 /etc/mosquitto/conf.d/nwia.conf
# Safely quote the validated hostname when writing a renewal hook.
cat > /etc/letsencrypt/renewal-hooks/deploy/nwia-mqtt <<HOOK
#!/bin/sh
set -eu
[ "\${RENEWED_LINEAGE:-}" = "/etc/letsencrypt/live/$broker_domain" ] || exit 0
install -m 0640 -o root -g mosquitto "\$RENEWED_LINEAGE/fullchain.pem" /etc/mosquitto/certs/fullchain.pem
install -m 0640 -o root -g mosquitto "\$RENEWED_LINEAGE/privkey.pem" /etc/mosquitto/certs/privkey.pem
systemctl restart mosquitto
HOOK
chmod 0750 /etc/letsencrypt/renewal-hooks/deploy/nwia-mqtt
systemctl enable mosquitto
systemctl restart mosquitto
systemctl --no-pager --full status mosquitto
printf '\nCredentials saved privately at /root/nwia-credentials.txt. Do not post them in chat.\n'
printf 'Verify TLS externally and then run the two-site acceptance checklist.\n'
