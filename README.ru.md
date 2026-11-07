# network-manager-amneziawg

[English](README.md) | **Русский**

VPN-плагин NetworkManager для **AmneziaWG**: тип **AmneziaWG** в «Настройки → Сеть → VPN»
и в `nm-connection-editor`, импорт и экспорт `.conf` из Amnezia.

> **Неофициальный проект**, не связан с командой [Amnezia](https://github.com/amnezia-vpn).
> Плагин использует их модуль ядра и утилиту `awg`. Об ошибках плагина пишите сюда.

## Установка

Ubuntu 24.04 и новее:

```sh
curl -fsSL https://github.com/MadeFrost/network-manager-amneziawg/releases/latest/download/install.sh | sh
```

Скрипт подключает [`ppa:amnezia/ppa`](https://launchpad.net/~amnezia/+archive/ubuntu/ppa)
(модуль ядра `amneziawg` и `amneziawg-tools`) и
[`ppa:madefrost/amneziawg`](https://launchpad.net/~madefrost/+archive/ubuntu/amneziawg)
(плагин), ставит заголовки ядра, если их нет, и проверяет загрузку модуля.

<details>
<summary>Ручная установка</summary>

```sh
sudo add-apt-repository ppa:amnezia/ppa
sudo add-apt-repository ppa:madefrost/amneziawg
sudo apt install network-manager-amneziawg
```

</details>

## Использование

1. В приложении Amnezia экспортируйте конфиг в формате **AmneziaWG**.
2. «Настройки → Сеть → VPN → +» → «Импорт из файла…».

Из консоли: `nmcli connection import type amneziawg file amnezia.conf`.

## Полезно знать

- Ключи хранятся в системном соединении, читать его может только root.
- `PreUp`, `PostUp`, `PreDown`, `PostDown`, `Table` и `SaveConfig` при импорте отбрасываются.
- Конфиги без параметров обфускации (Jc, S1, H1, I1…) импортируются как обычный WireGuard.
- Параметры, которых плагин не знает, передаются в AmneziaWG как есть, так что новые
  работают без обновления плагина.

## Ограничения

- Один `[Peer]` на соединение.
- Из `Address` берётся по одному адресу IPv4 и IPv6.
- Если в `AllowedIPs` есть `::/0`, а IPv6-адреса нет (обычный экспорт Amnezia), IPv6
  блокируется, как в `awg-quick`, а не идёт мимо VPN. У интерфейса при этом виден служебный
  адрес `fd61:7767::1/128`.
- Нужен модуль ядра, `amneziawg-go` не поддерживается.

## Если что-то не работает

- **AmneziaWG нет в списке VPN:** `sudo systemctl restart NetworkManager`.
- **Secure Boot.** DKMS подписывает модуль локальным ключом (MOK), и ядро примет его только
  после регистрации ключа: после перезагрузки в «MOK management» выберите
  *Enroll MOK → Continue* и введите пароль, заданный при установке. Если пароль не спрашивали:
  `sudo update-secureboot-policy --enroll-key` и перезагрузка. Признак проблемы:
  `modprobe amneziawg` пишет `Key was rejected by service`.
- **Журнал:** `journalctl -u NetworkManager -g amneziawg`.

## Разработка

Сборка, тесты, устройство и выпуск версий: [CONTRIBUTING.md](CONTRIBUTING.md) (на английском).

## Безопасность и лицензия

Об уязвимостях сообщайте не в issue, а как описано в [SECURITY.md](SECURITY.md).
Лицензия GPL-2.0-or-later ([LICENSE](LICENSE)), изменения по версиям в
[CHANGELOG.md](CHANGELOG.md).
