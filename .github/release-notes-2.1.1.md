## Melhorias

- O tutorial explica que, quando o aplicativo OAuth do Google está em **Teste**, é obrigatório adicionar a conta Google exata usada no login em **Público-alvo → Usuários de teste**.
- Uma recusa do Google encerra o login pendente e mostra orientações para resolver o acesso; a conta salva anteriormente é preservada.
- Campos em foco, botões, abas e dicas receberam ajustes de contraste e interação, mantendo o visual escuro inspirado no Chatterino. Os controles principais mostram seus atalhos.
- As buscas de mensagens e configurações têm um botão para limpar o texto. A busca em todos os chats aguarda a digitação, preserva o destino após reorganizar abas e avisa quando a mensagem já saiu do histórico.
- Quando o YouTube limita as solicitações (HTTP 429), o aplicativo mostra uma mensagem clara e aguarda dois minutos antes de tentar novamente.

## Downloads

- **Windows 11 x64:** baixe `YT-Chatterino-Windows11-2.1.1.zip`, extraia toda a pasta e abra `YT-Chatterino.exe`.
- **Arch Linux x86_64:** baixe o pacote `yt-chatterino-2.1.1-1-x86_64.pkg.tar.zst` e instale com `sudo pacman -U`.
- Código-fonte C++ e fontes das bibliotecas Qt estão em arquivos separados. As verificações SHA-256 acompanham os downloads.

A atualização automática preserva as configurações e contas salvas. O aplicativo continua independente e não é afiliado ao YouTube, à Twitch ou ao Chatterino.
