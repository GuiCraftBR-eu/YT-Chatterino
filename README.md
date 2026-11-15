<div align="center">

# YT-Chatterino

**Seus chats da Twitch, do YouTube e da Kick em uma só janela.**

Aplicativo independente, inspirado na interface do Chatterino. Disponível em português e inglês.

[Baixar a versão 2.5.5](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/tag/v2.5.5) · [Começar](#começar) · [OBS Studio](#painel-no-obs-studio) · [Ajuda](#ajuda-e-limitações)

</div>

## Baixar

| Plataforma | Arquivo | Como abrir |
| --- | --- | --- |
| Windows 11 | [Aplicativo para Windows](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/download/v2.5.5/YT-Chatterino-Windows11-2.5.5.zip) | Extraia o ZIP e abra `YT-Chatterino.exe`. Mantenha as pastas e DLLs junto ao executável. |
| Arch Linux x86_64 | [Pacote nativo](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/download/v2.5.5/yt-chatterino-2.5.5-1-x86_64.pkg.tar.zst) | Execute `sudo pacman -U ./yt-chatterino-2.5.5-1-x86_64.pkg.tar.zst`. |

Não é necessário instalar Python. Os arquivos e seus SHA-256 estão na [página da versão](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/tag/v2.5.5).

## Começar

1. Clique em **+** e cole um link ou digite o nome do canal. No YouTube, use o identificador do endereço, como `LofiGirl` para `youtube.com/@LofiGirl`.
2. Escolha entre abas, painéis separados ou chat misturado. Canais com nomes parecidos em outras plataformas podem ser sugeridos quando estiverem ao vivo: confira os nomes antes de selecionar, pois eles podem pertencer a pessoas diferentes.
3. Em **Configurações → Contas**, conecte as contas para enviar mensagens e usar as permissões de moderação disponíveis. Os guias ilustrados no aplicativo mostram como conectar YouTube e Kick.

Ler o chat público do YouTube normalmente não exige conta, chave de API ou unidades. Enviar mensagens e moderar exige a API oficial e consome cota. Se o Google limitar a leitura pública, é possível ativar a alternativa pela API oficial no menu do canal; ela também consome cota.

## Recursos

- **Organização:** abas, painéis divididos, chat misturado, canais salvos, janela separada e sobreposição com opacidade ajustável.
- **Conversas:** emotes nativos, BTTV e 7TV, preenchimento com Tab, respostas, histórico de envio, busca e destaques.
- **Moderação (beta):** quando seu cargo for reconhecido, a espada ativa botões ao lado das mensagens. Configure ações e ícones em **Configurações → Moderação**. As ações ainda exigem as permissões da plataforma.
- **Acompanhar:** alertas, enquetes, mensagens fixadas com links clicáveis e notificações de canais ao vivo quando disponíveis.
- **Celular:** no menu do painel, escolha **Abrir no celular** e leia o QR code. Computador e celular precisam estar na mesma rede confiável.
- **VODs:** a extensão do navegador sincroniza o chat com a reprodução. Abra a VOD no navegador e selecione-a pelo botão **+**. A disponibilidade depende da plataforma e do vídeo.

Para a extensão, use **+ → Configurar extensão do navegador…**. No Firefox, instale a [extensão assinada](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/download/v2.5.4/YT-Chatterino-Firefox-2.5.4.xpi); ela recebe atualizações assinadas automaticamente. No Chrome ou Edge, carregue a pasta `browser-extension` do aplicativo no modo de desenvolvedor.

## Painel no OBS Studio

O painel reúne os canais das suas contas em um único chat. As contas do OBS são **separadas** das do aplicativo independente; conecte-as em **Configurações → Contas** no próprio painel. No YouTube, é necessário haver uma live ativa.

**Windows:** baixe o [plugin para OBS](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/download/v2.5.5/YT-Chatterino-OBS-Windows-2.5.5.zip). Com o OBS fechado, extraia a pasta `yt-chatterino-obs` para `%PROGRAMDATA%\obs-studio\plugins` e reabra o OBS.

**Arch Linux:** baixe o [plugin nativo para OBS](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/download/v2.5.5/YT-Chatterino-OBS-ArchLinux-2.5.5.zip). Com o OBS fechado, extraia a pasta `yt-chatterino-obs` para `~/.local/share/obs-studio/plugins/` e reabra o OBS. O pacote é para o OBS do repositório do Arch, não para Flatpak.

Em ambos, ative **Painéis → YT-Chatterino**. Os plugins exigem OBS Studio 33 ou mais recente e são atualizados manualmente, substituindo a pasta com o OBS fechado.

## Ajuda e limitações

- **Google 403 / acesso negado:** em projetos OAuth em teste, adicione a conta Google em **Google Auth Platform → Público-alvo → Usuários de teste**. Consulte o [guia de login](docs/GOOGLE-LOGIN.md).
- Recursos dependem dos dados, das permissões e da disponibilidade das plataformas. O histórico da Twitch depende de serviço externo; a leitura da Kick usa uma interface pública não oficial.
- A extensão compartilha com o app local o estado das VODs, as lives abertas e painéis visíveis da Twitch. Votos e apostas são concluídos no navegador.
- As credenciais ficam no armazenamento protegido do sistema. Não compartilhe tokens nem arquivos OAuth. No Arch, é necessário um chaveiro compatível para salvar acessos.
- Quando houver atualização do aplicativo, use a seta na barra superior. Em caso de problema, consulte os guias [Windows](docs/README-PTBR.txt), [Arch Linux](docs/ARCH-LINUX.md) ou [abra uma issue](https://github.com/GuiCraftBR-eu/YT-Chatterino/issues).

## Código e avisos

[Compilar](docs/BUILDING.md) · [Histórico de alterações](CHANGELOG.md) · [Licenças](Licenses/NOTICE.txt)

YT-Chatterino é um projeto independente e não é afiliado ou endossado por YouTube/Google, Twitch/Amazon, Kick ou pelos responsáveis pelo Chatterino. Marcas e conteúdos de terceiros pertencem aos respectivos titulares.
