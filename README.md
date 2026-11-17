<div align="center">

# YT-Chatterino

**Seus chats da Twitch, do YouTube e da Kick em uma só janela.**

Aplicativo independente, inspirado na interface do Chatterino. Disponível em português e inglês.

[Baixar a versão 2.5.6](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/tag/v2.5.6) · [Começar](#começar) · [OBS Studio](#painel-no-obs-studio) · [Ajuda](#ajuda-e-limitações)

</div>

## Baixar

| Plataforma | Arquivo | Como abrir |
| --- | --- | --- |
| Windows 11 | [Aplicativo para Windows](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/download/v2.5.6/YT-Chatterino-Windows11-2.5.6.zip) | Extraia o ZIP e abra `YT-Chatterino.exe`. Mantenha as pastas e DLLs junto ao executável. |
| Linux x86_64 | [Instalador Flatpak](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/download/v2.5.6/YT-Chatterino-Flatpak-Install.py) | Instale Flatpak e Python 3 e execute `python3 YT-Chatterino-Flatpak-Install.py --app`. |

No Linux, o instalador configura verificações automáticas pelo systemd de usuário, além de instalar o pacote Flatpak. É possível instalar o [arquivo Flatpak](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/download/v2.5.6/YT-Chatterino-Linux-2.5.6.flatpak) manualmente, mas nesse caso as atualizações não são configuradas automaticamente. O aplicativo não precisa de Python para funcionar. Os arquivos e seus SHA-256 estão na [página da versão](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/tag/v2.5.6).

Ao migrar do pacote nativo, o instalador copia as configurações antigas se ainda não existir um perfil Flatpak, sem apagar o original. Conecte as contas novamente no aplicativo e no painel do OBS: o armazenamento de credenciais é separado.

## Começar

1. Clique em **+** e cole um link ou digite o nome do canal. No YouTube, use o identificador do endereço, como `LofiGirl` para `youtube.com/@LofiGirl`.
2. Escolha entre abas, painéis separados ou chat misturado. Canais com nomes parecidos em outras plataformas podem ser sugeridos quando estiverem ao vivo: confira os nomes antes de selecionar, pois eles podem pertencer a pessoas diferentes.
3. Em **Configurações → Contas**, conecte as contas para enviar mensagens e usar as permissões de moderação disponíveis. Os guias ilustrados no aplicativo mostram como conectar YouTube e Kick.

Ler o chat público do YouTube normalmente não exige conta, chave de API ou unidades. Enviar mensagens e moderar exige a API oficial e consome cota. Se o Google limitar a leitura pública, é possível ativar a alternativa pela API oficial no menu do canal; ela também consome cota.

## Recursos

- **Organização:** abas, painéis divididos, chat misturado, canais salvos, janela separada e sobreposição com opacidade ajustável.
- **Conversas:** emotes nativos, BTTV e 7TV, preenchimento com Tab, respostas, histórico de envio, busca e destaques.
- **Moderação:** quando seu cargo for reconhecido, a espada ativa botões ao lado das mensagens. Configure ações e ícones em **Configurações → Moderação**. As ações exigem as permissões da plataforma; reconecte a Twitch para conceder as novas permissões de AutoMod e modos de chat.
- **Comandos:** digite `/` no início da mensagem e use Tab para completar os comandos disponíveis. `/ban`, `/timeout` e `/unban` usam as permissões da plataforma; `/mod`, `/unmod`, `/vip` e `/unvip` estão disponíveis onde a API oferece esses cargos. Na Twitch, o nome pode ser consultado pela API; nas outras plataformas, o usuário precisa aparecer no histórico recente do painel. Comandos de bots como `!comando` continuam sendo enviados como mensagens normais. O cartão de usuário também oferece os botões de cargos para o dono do canal.
- **Acompanhar:** alertas, enquetes, mensagens fixadas com links clicáveis e notificações de canais ao vivo quando disponíveis.
- **Celular:** no menu do painel, escolha **Abrir no celular** e leia o QR code. Computador e celular precisam estar na mesma rede confiável.
- **VODs:** a extensão do navegador sincroniza o chat com a reprodução. Abra a VOD no navegador e selecione-a pelo botão **+**. A disponibilidade depende da plataforma e do vídeo.

Para a extensão, use **+ → Configurar extensão do navegador…**. No Firefox, instale a [extensão assinada](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/download/v2.5.6/YT-Chatterino-Firefox-2.5.6.xpi); ela recebe atualizações assinadas automaticamente. No Chrome ou Edge, carregue a pasta `browser-extension` do aplicativo no modo de desenvolvedor.

## Painel no OBS Studio

O painel reúne os canais das suas contas em um único chat. As contas do OBS são **separadas** das do aplicativo independente; conecte-as em **Configurações → Contas** no próprio painel. No YouTube, é necessário haver uma live ativa.

**Windows:** baixe o [plugin para OBS](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/download/v2.5.6/YT-Chatterino-OBS-Windows-2.5.6.zip). Com o OBS fechado, extraia a pasta `yt-chatterino-obs` para `%PROGRAMDATA%\obs-studio\plugins` e reabra o OBS. É necessário instalar a 2.5.6 manualmente uma vez; depois, o plugin verifica atualizações e as aplica ao reiniciar o OBS.

**Linux:** instale primeiro o OBS Studio Flatpak pelo Flathub e execute `python3 YT-Chatterino-Flatpak-Install.py --obs`. Use `--both` para instalar o aplicativo e o plugin. O instalador configura atualizações para seu usuário; reinicie o OBS depois de uma atualização. O plugin é para o OBS Flatpak.

Em ambos, ative **Painéis → YT-Chatterino**. Os plugins exigem OBS Studio 33 ou mais recente.

## Ajuda e limitações

- **Google 403 / acesso negado:** em projetos OAuth em teste, adicione a conta Google em **Google Auth Platform → Público-alvo → Usuários de teste**. Consulte o [guia de login](docs/GOOGLE-LOGIN.md).
- Recursos dependem dos dados, das permissões e da disponibilidade das plataformas. O histórico da Twitch depende de serviço externo; a leitura da Kick usa uma interface pública não oficial.
- A extensão compartilha com o app local o estado das VODs, as lives abertas e painéis visíveis da Twitch. Enquetes gratuitas da Twitch podem ser votadas no app quando a enquete estiver aberta na aba conectada. Apostas com pontos e votos no YouTube ou na Kick continuam no navegador.
- A votação pelo app foi testada em navegador com uma página simulada; a confirmação em uma enquete real autenticada ainda está pendente.
- As credenciais ficam no armazenamento protegido do sistema. Não compartilhe tokens nem arquivos OAuth. No Arch, é necessário um chaveiro compatível para salvar acessos.
- No Windows, use a seta na barra superior para atualizar o aplicativo. No Linux, consulte o [guia Flatpak](docs/FLATPAK-LINUX.md). Para relatar problemas, [abra uma issue](https://github.com/GuiCraftBR-eu/YT-Chatterino/issues).

## Código e avisos

[Compilar](docs/BUILDING.md) · [Histórico de alterações](CHANGELOG.md) · [Licenças](Licenses/NOTICE.txt)

YT-Chatterino é um projeto independente e não é afiliado ou endossado por YouTube/Google, Twitch/Amazon, Kick ou pelos responsáveis pelo Chatterino. Marcas e conteúdos de terceiros pertencem aos respectivos titulares.
