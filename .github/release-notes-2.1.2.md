## Novidades

- Passe o mouse sobre um emote no chat para ver seu nome, provedor e uma prévia.
- Quando o YouTube limitar a leitura pelo site (HTTP 429), ative por chat a alternativa opcional da API oficial: clique com o botão direito na aba, abra o menu do canal e escolha **Usar API oficial se necessário**. É necessário conectar uma conta do YouTube em **Configurações → Contas** e ativar a API no projeto Google.
- A alternativa usa **5 unidades por solicitação**, aproximadamente **986 unidades por hora por chat** no intervalo padrão de 18,25 segundos, além de outras ações. A cota exibida é uma estimativa. A leitura comum continua sem custo de API; a alternativa permanece desativada até você ativá-la.
- As tentativas de vários chats são coordenadas durante bloqueios temporários. O bloqueio HTTP 429 continua dependendo do Google, e a API oficial também pode recusar solicitações ou impor seus próprios limites.

## Downloads

- **Windows 11 x64:** baixe `YT-Chatterino-Windows11-2.1.2.zip`, extraia tudo e abra `YT-Chatterino.exe`.
- **Arch Linux x86_64:** baixe `yt-chatterino-2.1.2-1-x86_64.pkg.tar.zst` e instale com `sudo pacman -U`.
- Código-fonte C++ e fontes das bibliotecas Qt estão disponíveis separadamente. Os arquivos SHA-256 acompanham os downloads.

O aplicativo é independente e não é afiliado ao YouTube, à Twitch ou ao Chatterino.
