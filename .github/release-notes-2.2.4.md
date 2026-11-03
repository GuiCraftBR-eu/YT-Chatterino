## Novidades e correções

- Corrige reconexões desnecessárias e mensagens repetidas de conexão da Twitch.
- O chat ao vivo não espera mais o histórico da Twitch; se o serviço externo falhar, o aplicativo tenta novamente.
- Históricos disponíveis de todos os canais são combinados em ordem em chats mistos.
- Mensagens enviadas pelo usuário recebem seus emblemas e sua cor informados pela Twitch para cada canal.
- Mostra o ícone do canal no cabeçalho e no menu.
- Exibe os controles de moderação e edição conforme o cargo identificado. No YouTube, o menu de edição mostra apenas **Alterar título**.

O histórico da Twitch depende de um serviço externo e pode não existir para todos os canais. No YouTube de outra pessoa, os controles de moderação aparecem quando o app vê o emblema de moderador em uma mensagem recente da conta conectada. A API da Twitch permite editar o título e a categoria pelo app apenas na conta do próprio canal.

## Downloads

- **Windows 11 x64:** baixe `YT-Chatterino-Windows11-2.2.4.zip`, extraia tudo e abra `YT-Chatterino.exe`.
- **Arch Linux x86_64:** baixe `yt-chatterino-2.2.4-1-x86_64.pkg.tar.zst` e instale com `sudo pacman -U`.
- O código-fonte C++, as fontes das bibliotecas Qt e os arquivos SHA-256 acompanham a versão.

O aplicativo é independente e não é afiliado ao YouTube, à Twitch ou ao Chatterino.
