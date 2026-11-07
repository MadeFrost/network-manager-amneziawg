#!/bin/sh
# Installs network-manager-amneziawg on Ubuntu:
#   1. ppa:amnezia/ppa: the amneziawg kernel module (DKMS) and amneziawg-tools;
#   2. ppa:madefrost/amneziawg: the plugin.
# Steps already done are skipped, so running it again is safe (and updates the plugin).
#
# Usage: install.sh [-y] [path/to/network-manager-amneziawg_*.deb]
#   -y     do not ask for confirmation
#   .deb   install the plugin from this file instead of the PPA

set -eu

AWG_PPA="amnezia/ppa"
PLUGIN_PPA="madefrost/amneziawg"
PKG="network-manager-amneziawg"
ASSUME_YES=0
DEB=""

# Language as gettext picks it: LANGUAGE counts only with a non-C locale.
loc=${LC_ALL:-${LC_MESSAGES:-${LANG:-}}}
case "$loc" in
C|C.*|POSIX|"") lang=en ;;
*) lang=${LANGUAGE:-$loc} ;;
esac
case "$lang" in ru*) RU=1 ;; *) RU=0 ;; esac
# m "English" "Русский": the message in the system language.
m() { if [ $RU -eq 1 ]; then printf '%s' "$2"; else printf '%s' "$1"; fi; }
say()  { printf '\033[1m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m%s\033[0m %s\n' "$(m 'Warning:' 'Внимание:')" "$*" >&2; }
die()  { printf '\033[1;31m%s\033[0m %s\n' "$(m 'Error:' 'Ошибка:')" "$*" >&2; exit 1; }

usage() {
	m 'Usage: install.sh [-y] [path/to/network-manager-amneziawg_*.deb]
  -y     do not ask for confirmation
  .deb   install the plugin from this file instead of ppa:madefrost/amneziawg
' 'Использование: install.sh [-y] [путь/к/network-manager-amneziawg_*.deb]
  -y     не спрашивать подтверждения
  .deb   поставить плагин из этого файла, а не из ppa:madefrost/amneziawg
'
}

for arg in "$@"; do
	case "$arg" in
	-y|--yes) ASSUME_YES=1 ;;
	-h|--help) usage; exit 0 ;;
	-*) die "$(m "unknown option: $arg" "неизвестный параметр: $arg")" ;;
	*) DEB="$arg" ;;
	esac
done

# --- checks ------------------------------------------------------------------

[ -r /etc/os-release ] && . /etc/os-release
[ "${ID:-}" = "ubuntu" ] || case " ${ID_LIKE:-} " in
	*" ubuntu "*) ;;
	*) die "$(m 'this script is for Ubuntu and its derivatives: the PPAs are built for Ubuntu only.' \
	            'скрипт рассчитан на Ubuntu и производные: PPA собираются только для неё.')" ;;
esac
command -v apt-get >/dev/null || die "$(m 'apt-get not found.' 'не найден apt-get.')"

if [ -n "$DEB" ]; then
	[ -f "$DEB" ] || die "$(m "file not found: $DEB" "файл не найден: $DEB")"
	# apt treats a bare name as a package name; it needs a path.
	DEB=$(cd "$(dirname "$DEB")" && pwd)/$(basename "$DEB")
fi

if [ "$(id -u)" -eq 0 ]; then
	SUDO=""
else
	command -v sudo >/dev/null || die "$(m 'root is needed: run as root or install sudo.' \
	                                       'нужны права root: запустите скрипт от root или установите sudo.')"
	SUDO="sudo"
fi

ppa_added() {
	grep -rqsE "ppa\.launchpad(content)?\.net/$1" /etc/apt/sources.list /etc/apt/sources.list.d/
}
pkg_installed() {
	dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q "install ok installed"
}

need_awg_ppa=1;    ppa_added "$AWG_PPA" && need_awg_ppa=0
need_plugin_ppa=0; [ -z "$DEB" ] && ! ppa_added "$PLUGIN_PPA" && need_plugin_ppa=1
need_awg=1;        pkg_installed amneziawg-tools && modinfo amneziawg >/dev/null 2>&1 && need_awg=0
headers="linux-headers-$(uname -r)"
need_headers=0;    [ $need_awg -eq 1 ] && ! pkg_installed "$headers" && need_headers=1
plugin=${DEB:+$(basename "$DEB")}
plugin=${plugin:-$PKG}

# --- plan --------------------------------------------------------------------

m 'To do:' 'Будет сделано:'; echo
[ $need_awg_ppa -eq 1 ]    && echo "  • $(m "add ppa:$AWG_PPA (kernel module and awg from the AmneziaWG developers)" \
                                         "подключить ppa:$AWG_PPA (модуль ядра и утилита awg от разработчиков AmneziaWG)")"
[ $need_plugin_ppa -eq 1 ] && echo "  • $(m "add ppa:$PLUGIN_PPA (this plugin)" "подключить ppa:$PLUGIN_PPA (этот плагин)")"
[ $need_headers -eq 1 ]    && echo "  • $(m "install $headers (to build the module)" "установить $headers (нужны, чтобы собрать модуль)")"
[ $need_awg -eq 1 ]        && echo "  • $(m 'install amneziawg (kernel module via DKMS, amneziawg-tools)' \
                                         'установить amneziawg (модуль ядра через DKMS, amneziawg-tools)')"
echo "  • $(m "install the plugin: $plugin" "установить плагин: $plugin")"
if [ $need_awg_ppa -eq 1 ] || [ $need_plugin_ppa -eq 1 ]; then
	echo
	m 'Adding a PPA means trusting its owner: its packages are installed as root and
are updated with the system. To remove: sudo add-apt-repository --remove ppa:<name>
' 'Подключая PPA, вы доверяете его владельцу: пакеты оттуда ставятся с правами root
и обновляются вместе с системой. Удалить: sudo add-apt-repository --remove ppa:<имя>
'
fi
echo

if [ $ASSUME_YES -eq 0 ]; then
	m 'Continue? [y/N] ' 'Продолжить? [y/N] '
	# With `curl ... | sh` stdin is the script itself: ask the terminal.
	answer=""
	if [ -t 0 ]; then
		read -r answer || true
	else
		{ read -r answer </dev/tty; } 2>/dev/null || true
	fi
	case "$answer" in
	y|Y|yes|д|Д|да|Да) ;;
	*) echo; m 'Cancelled.' 'Отменено.'; echo; exit 1 ;;
	esac
fi

# --- install -----------------------------------------------------------------

if [ $need_awg_ppa -eq 1 ] || [ $need_plugin_ppa -eq 1 ]; then
	if ! command -v add-apt-repository >/dev/null; then
		say "$(m 'Installing software-properties-common (for add-apt-repository)' \
		         'Устанавливаю software-properties-common (для add-apt-repository)')"
		$SUDO apt-get update
		$SUDO apt-get install -y software-properties-common
	fi
	for ppa in $( [ $need_awg_ppa -eq 1 ] && echo "$AWG_PPA"; [ $need_plugin_ppa -eq 1 ] && echo "$PLUGIN_PPA" ); do
		say "$(m "Adding ppa:$ppa" "Подключаю ppa:$ppa")"
		$SUDO add-apt-repository -y -n "ppa:$ppa"
	done
fi
$SUDO apt-get update

if [ $need_awg -eq 1 ]; then
	pkgs="amneziawg"
	[ $need_headers -eq 1 ] && pkgs="$headers $pkgs"
	say "$(m "Installing $pkgs (building the kernel module takes a couple of minutes)" \
	         "Устанавливаю $pkgs (сборка модуля ядра может занять пару минут)")"
	# shellcheck disable=SC2086
	$SUDO apt-get install -y $pkgs
fi

say "$(m 'Installing the plugin' 'Устанавливаю плагин')"
if [ -n "$DEB" ]; then
	$SUDO apt-get install -y "$DEB"
else
	apt-cache show "$PKG" >/dev/null 2>&1 \
		|| die "$(m "ppa:$PLUGIN_PPA has no $PKG for this Ubuntu release (${VERSION_CODENAME:-?})." \
		            "в ppa:$PLUGIN_PPA нет $PKG для этой версии Ubuntu (${VERSION_CODENAME:-?}).")"
	$SUDO apt-get install -y "$PKG"
fi

# --- post-install checks -----------------------------------------------------

echo
module_ok=0
if lsmod | grep -q '^amneziawg ' || $SUDO modprobe amneziawg 2>/dev/null; then
	module_ok=1
	say "$(m 'The amneziawg kernel module is loaded.' 'Модуль ядра amneziawg загружен.')"
else
	warn "$(m 'the amneziawg kernel module did not load.' 'модуль ядра amneziawg не загрузился.')"
	if command -v mokutil >/dev/null && mokutil --sb-state 2>/dev/null | grep -qi 'enabled'; then
		m '  Secure Boot is on: the kernel does not load an unenrolled module.
  If DKMS asked for a password during installation, reboot and in the blue
  "MOK management" screen choose Enroll MOK, Continue, then enter that password.
  If no password was asked: sudo update-secureboot-policy --enroll-key, then reboot.
' '  Включён Secure Boot: неподписанный модуль ядро не загрузит.
  Если при установке DKMS просил придумать пароль, перезагрузитесь и в синем
  экране «MOK management» выберите Enroll MOK, Continue и введите этот пароль.
  Если пароль не спрашивали: sudo update-secureboot-policy --enroll-key, затем перезагрузка.
'
	else
		m '  Check the module build: dkms status amneziawg; details: sudo modprobe amneziawg
' '  Проверьте сборку модуля: dkms status amneziawg; подробности: sudo modprobe amneziawg
'
	fi
fi

echo
say "$(m 'Done.' 'Готово.')"
m '  The AmneziaWG VPN type is in Settings, Network, VPN, "+" (also "Import from file...").
  If it is missing: sudo systemctl restart NetworkManager and reopen Settings.
' '  Тип AmneziaWG появится в «Настройки → Сеть → VPN → +» и в «Импорт из файла…».
  Если его нет в списке: sudo systemctl restart NetworkManager и переоткройте Настройки.
'
[ $module_ok -eq 1 ] || m '  The connection works once the kernel module is loaded (see above).
' '  Подключение заработает после того, как модуль ядра будет загружен (см. выше).
'
