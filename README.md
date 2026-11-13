<div align="center">

# YT-Chatterino

**Seus chats ao vivo, em uma só janela.**

Cliente independente para chats do YouTube, da Twitch e da Kick, com abas, painéis e emotes.

**Windows 11 · Arch Linux · C++20 / Qt 6 · PT-BR / English**

[Baixar v2.5.4](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/tag/v2.5.4) · [Como usar](#primeiros-passos) · [Compilar](docs/BUILDING.md) · [Aviso de independência](#independência-e-marcas)

</div>

> [!IMPORTANT]
> **Projeto independente e não oficial.** O YT-Chatterino não é afiliado, associado, patrocinado, aprovado nem mantido pelo YouTube/Google, pela Twitch/Amazon, pela Kick ou pelo projeto Chatterino e seus responsáveis. Não é uma versão oficial do Chatterino. Os nomes são mencionados apenas para identificar compatibilidade e a inspiração da interface.

## Baixar e abrir

| Sistema | Download da versão 2.5.4 | Instalação |
| --- | --- | --- |
| **Windows 11** | [Baixar ZIP](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/download/v2.5.4/YT-Chatterino-Windows11-2.5.4.zip) | Extraia tudo e abra `YT-Chatterino.exe`. Mantenha as DLLs e pastas junto do executável. |
| **Arch Linux** | [Baixar pacote nativo](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/download/v2.5.4/yt-chatterino-2.5.4-1-x86_64.pkg.tar.zst) | Instale com o comando abaixo. [Guia para Arch](docs/ARCH-LINUX.md). |

```sh
sudo pacman -U ./yt-chatterino-2.5.4-1-x86_64.pkg.tar.zst
```

Não precisa de Python. Baixe pelo repositório oficial **deste projeto**; os arquivos e as verificações SHA-256 estão na [página da versão](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/tag/v2.5.4).

## Primeiros passos

### Painel no OBS Studio (Windows)

Baixe o [plugin para OBS 33 ou mais recente](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/download/v2.5.4/YT-Chatterino-OBS-Windows-2.5.4.zip). Feche o OBS, extraia a pasta `yt-chatterino-obs` do ZIP para `%PROGRAMDATA%\obs-studio\plugins` e reinicie o OBS. Em uma instalação portátil, use a pasta `plugins` dentro da instalação. Ative **Painéis → YT-Chatterino**.

No painel, abra **Configurações → Contas** e conecte suas contas da Twitch, do YouTube e da Kick; os acessos são separados dos salvos no aplicativo independente. Os canais conectados aparecem juntos em um único chat. O YouTube precisa ter uma transmissão ao vivo; se houver mais de uma, escolha qual abrir. A extensão atualizada pode atender ao aplicativo e ao painel do OBS simultaneamente. Para atualizar o plugin, feche o OBS e substitua a pasta pela da nova versão. O plugin é para Windows; o aplicativo independente também está disponível para Arch Linux.

### Aplicativo independente

1. Clique em **+** ou pressione **Ctrl+T**. Escolha a plataforma e digite nomes ou links separados por espaço. No YouTube, use o identificador do link, por exemplo `LofiGirl` para `youtube.com/@LofiGirl`; o `@` é opcional. Na lista, você também pode selecionar canais salvos; o app procura as transmissões ao vivo do canal.
2. Para conectar a Twitch, abra **Configurações → Contas** e autorize o acesso no navegador.
3. Para abrir um chat da Kick, cole o link ou selecione Kick e digite o nome do canal. Para enviar ou moderar, crie seu aplicativo em [Kick Developer](https://kick.com/settings/developer) e siga o **guia ilustrado da Kick** em Configurações → Tutorial. O endereço de redirecionamento é `http://localhost:37365/callback`. Em **Configurações → Contas**, conecte sua conta e autorize as permissões desejadas.
4. Para enviar mensagens, moderar ou editar títulos no YouTube, ative a **YouTube Data API v3** no Google Cloud, crie um cliente **OAuth para computador** e selecione o JSON em **Contas**. Depois, conecte sua conta.

A leitura pública do YouTube não exige login nem chave de API e não usa unidades da API oficial. Os lotes chegam mais rapidamente quando possível e são distribuídos na tela; após limitação, o app respeita temporariamente o intervalo mais conservador informado pelo YouTube. Envio e moderação usam a API oficial e suas cotas; moderar ou editar uma transmissão exige as permissões correspondentes no canal. Se o YouTube retornar HTTP 429, você pode ativar por chat a alternativa pela API oficial no menu do canal. Ela exige a conta conectada, consome 5 unidades por solicitação e usa aproximadamente 986 unidades por hora por chat no intervalo padrão de 18,25 segundos; vários chats consomem mais. A cota exibida é uma estimativa, e a API também pode impor limites ou negar acesso.

O **Tutorial**, nas configurações, explica as conexões e os recursos. Para conectar o YouTube, abra o **guia com imagens** em Configurações → Contas ou no tutorial. As credenciais usam o armazenamento protegido do sistema; não compartilhe tokens, perfis ou seu JSON OAuth. No Arch, salvar acessos exige um [chaveiro compatível](docs/ARCH-LINUX.md).

**Google bloqueou o login com 403 / `access_denied`?** Se o projeto estiver em teste, seu responsável precisa adicionar a conta Google exata em **Google Auth Platform → Público-alvo → Usuários de teste**. Depois, cancele o login pendente e tente novamente. Veja o [guia de solução](docs/GOOGLE-LOGIN.md), incluindo a opção de usar seu próprio projeto.

## O que você pode fazer

- **Organizar os chats:** abas, painéis lado a lado ou empilhados e painel misto com ícones de canais.
- **Alertas:** canais abertos da Twitch e Kick podem avisar quando entram ao vivo; é possível adicionar outros canais nas configurações.
- **Conversar:** respostas na Twitch, preenchimento de nomes de exibição, histórico de mensagens enviadas e seletor de emotes nativos, BTTV e 7TV. O cartão de usuário mostra a data de seguimento quando a plataforma permite: na Twitch, a consulta de outras pessoas exige ser dono ou moderador e autorizar a leitura de seguidores; no YouTube, só funciona para inscrições públicas com conta conectada. A API pública da Kick não fornece essa data.
- **Abrir no celular:** clique com o botão direito na aba, escolha o painel e **Abrir no celular**. Inicie na rede local e leia o QR code ou informe o endereço e código no navegador do celular. Computador e celular precisam estar na mesma rede, com o app aberto. A janela do QR code pode ser fechada; clique em **Celular** no painel para reabri-la ou encerrar o acesso. A versão para celular oferece leitura, envio, respostas, emotes e moderação disponível no painel; não inclui todos os recursos do aplicativo. A conexão usa HTTP local: use uma rede confiável e pressione **Parar** para encerrar o acesso. Se o Windows solicitar acesso pela rede, permita-o apenas em redes privadas; o firewall pode bloquear a conexão até ser configurado.
- **Modos do chat:** a barra abaixo do canal mostra os modos da Twitch e do Kick quando disponíveis, incluindo o tempo mínimo para seguidores e o intervalo do modo lento. No YouTube, mostra o último aviso de modo recebido; a plataforma nem sempre fornece o estado atual.
- **No celular:** toque no nome de uma pessoa para ver seu histórico armazenado e toque na prévia de uma resposta para abrir a conversa e em emotes ou emblemas para ampliá-los. Usuários autorizados podem moderar e editar o título ou categoria pelo celular. A atualização é enviada assim que o computador recebe mensagens.
- **Horário:** em Configurações → Aparência, ajuste separadamente o tamanho da fonte do horário.
- **Mensagens removidas:** mostra se foram excluídas, se o chat foi limpo ou se o autor recebeu banimento ou timeout (com duração), quando a plataforma informa esses detalhes. Sem informação suficiente, mostra apenas que foram removidas.
- **Acompanhar mensagens:** opção de pausar a rolagem ao passar o mouse (as mensagens continuam chegando), prévias de emotes mais estáveis, pesquisa, filtros, destaques e sons; escolha cores separadas para alertas e menções nas configurações; histórico padrão de 2.000 mensagens por painel. Um botão aparece no cabeçalho quando há filtros ativos; clique nele para revisá-los.
- **Personalizar:** configurações organizadas por categorias, pesquisa que abre a opção encontrada e botão Aplicar sempre visível; ajuste fontes, emotes, emblemas, cores, horários e janela sempre no topo.
- **Acompanhar a transmissão:** cabeçalho com duração e campos configuráveis; arraste o título para ajustar sua altura; avisos de inscritos, membros e recompensas reconhecidos pelo chat.
- **Mensagens excluídas:** opção para manter visíveis, esmaecidas, as mensagens que o app recebeu antes da exclusão.
- **Moderar:** ações conforme as permissões da sua conta e os recursos de cada plataforma.

A leitura da Kick usa a conexão pública do site, não documentada oficialmente. Ela entrega mensagens em tempo real e recupera o histórico recente após desconexões, mas pode mudar ou perder mensagens durante falhas prolongadas. Alertas dependem dos eventos disponibilizados pela plataforma. Para moderar ou editar seu canal, habilite as permissões correspondentes no aplicativo de desenvolvedor e reconecte a conta; ações de moderação exigem o cargo no canal.

Mensagens fixadas do YouTube usam os dados disponíveis no chat do site; a API oficial de leitura não informa essas fixações. Na Twitch, a extensão também pode mostrar a mensagem fixada e os painéis de enquetes e apostas visíveis no chat aberto no navegador, sem acesso de moderador. Abra o painel na Twitch para compartilhar suas opções. A disponibilidade depende do que o site exibe e pode mudar quando a Twitch altera a interface. Para votar ou apostar, use o botão que abre a Twitch e conclua a ação no navegador. A API oficial continua disponível para canais e contas com as permissões necessárias. Enquetes do YouTube dependem dos dados recebidos do chat ou da API oficial; quando só houver a última contagem observada, ela será identificada como tal.

Quando a Kick está offline, a API pode ocultar o título atual. Ao alterar só a categoria, deixe o título em branco para preservá-lo.

Os controles de moderação aparecem quando o app identifica seu cargo no canal. No YouTube de outra pessoa, isso depende de o app receber uma mensagem recente da sua conta com o emblema de moderador. O histórico recente da Twitch depende de um serviço externo e pode estar indisponível; o chat ao vivo continua independente dele.

### Replay de chat de VOD

O chat acompanha a posição, a pausa e os saltos da VOD aberta no navegador. No app, clique em **+ → Configurar extensão do navegador…** para abrir as instruções e a pasta da extensão. No Chrome/Edge, ative o modo de desenvolvedor em Extensões e carregue a pasta `browser-extension` sem compactação. No Firefox, baixe a [extensão assinada](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/download/v2.5.4/YT-Chatterino-Firefox-2.5.4.xpi), abra `about:addons` e use **engrenagem → Instalar extensão de arquivo**. Instale-a uma vez para receber as próximas versões assinadas automaticamente. Se já usava a extensão anterior, instale a nova versão assinada; no Chrome/Edge, recarregue a pasta atualizada.

Com o app e a extensão abertos, comece a reproduzir uma VOD compatível, clique em **+**, marque-a em **Selecionar chats de VOD abertos** e clique em **Abrir**. O app não abre abas automaticamente; se fechar o chat, você pode selecioná-lo novamente enquanto o vídeo estiver aberto. Para misturar VODs, selecione **Chat misturado** e marque os vídeos desejados. Também é possível selecionar **Link de VOD** e adicionar links manualmente; mantenha as VODs abertas no navegador para sincronizar a reprodução.

O botão **+** também mostra lives abertas no navegador. Quando os nomes dos canais são parecidos, uma sugestão de chat misturado permite selecioná-los juntos. Confira a sugestão antes de abrir: nomes parecidos não garantem que os canais sejam da mesma pessoa.

O replay é somente leitura e depende do chat estar disponível na Twitch, no YouTube ou na Kick. As interfaces públicas não oficiais podem mudar ou limitar o acesso; mensagens, emotes ou distintivos podem faltar. A extensão envia ao app local endereços de canais e vídeos compatíveis, o estado de reprodução da VOD e os painéis visíveis da Twitch (como mensagens fixadas e enquetes). Ela não coleta mensagens comuns do chat ou credenciais. A conexão aceita extensões do navegador e fica restrita ao próprio computador.

### Controles essenciais

| Ação | Controle |
| --- | --- |
| Adicionar chat / painel | **Ctrl+T** / **Ctrl+Shift+T** |
| Fechar painel | **Ctrl+W** |
| Pesquisar no painel / em todos | **Ctrl+F** / **Ctrl+Shift+F** |
| Responder na Twitch / mencionar no YouTube | Clique direito na mensagem; em chats misturados, o canal correspondente também é selecionado |
| Abrir ações e moderação | **Ctrl + clique direito** na mensagem |
| Completar emotes ou nomes | **Tab / Shift+Tab** |
| Navegar pelas mensagens enviadas nesta sessão | **↑ / ↓** no campo de mensagem |
| Abrir histórico de usuário | Clique no nome ou em uma menção **@usuário** |
| Rolar automaticamente | Clique com a roda e mova o mouse; **Esc** para parar |

Mais opções em **Atalhos de teclado** e nos guias de uso em [português](docs/README-PTBR.txt) e [inglês](docs/README.txt).

## Atualizações e ajuda

Quando houver uma nova versão, clique na seta de download da barra superior e confirme **Instalar atualização e reiniciar**. As configurações e contas salvas são preservadas, com uma cópia de recuperação.

<details>
<summary>Atualizando uma instalação antiga?</summary>

Instalações antigas pelo código-fonte e o pacote Arch baseado em Python precisam de atualização manual para a versão nativa. Nas versões nativas 2.0.0/2.0.1, mova atalhos `.lnk` temporariamente para fora da pasta se impedirem a atualização; coloque-os de volta depois. Esse problema foi corrigido na 2.0.2.

</details>

Mudanças nas plataformas podem interromper integrações. Consulte os [limites conhecidos e instruções de uso](docs/README-PTBR.txt) ou [relate um problema](https://github.com/GuiCraftBR-eu/YT-Chatterino/issues).

## Código e documentação

[Compilação](docs/BUILDING.md) · [Histórico de alterações](CHANGELOG.md) · [Verificação da v2.1.1](docs/VERIFICACAO-2.1.1.md) · [Benchmark 1.3.0 → 2.1.0](docs/BENCHMARK-1.3.0-2.1.0.md)

O código da aplicação está em [`src/`](src/). O README e as notas de versão são mantidos em português brasileiro.

## Independência e marcas

**YT-Chatterino é um projeto independente, sem vínculo institucional ou comercial com YouTube, Google, Twitch, Amazon ou Chatterino e seus responsáveis.** A compatibilidade com serviços de terceiros e a inspiração visual no Chatterino não indicam parceria, autorização, certificação ou endosso.

Os nomes, marcas, logotipos, emotes e demais conteúdos de terceiros pertencem aos respectivos titulares. Sua referência neste projeto não transfere direitos sobre esses elementos nem concede licença para usá-los. O uso das plataformas continua sujeito aos respectivos termos, políticas e à legislação aplicável, inclusive a brasileira.

As licenças e os avisos das dependências estão em [`Licenses/`](Licenses/); os fontes correspondentes às bibliotecas Qt distribuídas acompanham os arquivos da versão. Essas licenças se aplicam às respectivas dependências e não concedem automaticamente uma licença geral ao código ou à arte deste projeto; veja o [aviso de licenças](Licenses/NOTICE.txt).
