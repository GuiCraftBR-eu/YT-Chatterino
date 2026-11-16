# Linux — Flatpak

Instale Flatpak, Python 3 e uma sessão de usuário com systemd. Baixe `YT-Chatterino-Flatpak-Install.py` da página da versão e execute, sem sudo:

```sh
python3 YT-Chatterino-Flatpak-Install.py --app   # aplicativo
python3 YT-Chatterino-Flatpak-Install.py --obs   # plugin do OBS Flatpak
python3 YT-Chatterino-Flatpak-Install.py --both  # ambos
```

Para o plugin, instale primeiro o OBS Studio Flatpak pelo Flathub. O instalador verifica tamanho e SHA-256 dos arquivos informados pelo GitHub, instala para o usuário e ativa um timer systemd para verificar atualizações a cada seis horas. Reinicie o OBS para carregar um plugin atualizado. A instalação direta de um `.flatpak` não configura esse timer.

O instalador copia, sem sobrescrever ou excluir, as configurações do aplicativo ou do painel OBS nativo que ainda não tenham sido migradas. As credenciais ficam separadas no ambiente Flatpak: conecte novamente as contas. Um serviço de chaveiro compatível é necessário para persistir logins.

Para conferir ou desativar as verificações:

```sh
systemctl --user status yt-chatterino-flatpak-update.timer
systemctl --user disable --now yt-chatterino-flatpak-update.timer
```

Os pacotes não são publicados no Flathub. O plugin exige a versão Flatpak do OBS Studio 33 ou superior; não funciona no OBS nativo.
