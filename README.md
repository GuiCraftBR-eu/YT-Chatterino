# YT-Chatterino

Cliente de chat para **YouTube e Twitch**, feito em **C++20 e Qt 6**, com visual inspirado no Chatterino. Disponível para Windows 11 e Arch Linux nativo.

## Baixar e abrir

- **[Windows 11 — versão 2.1.0](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/download/v2.1.0/YT-Chatterino-Windows11-2.1.0.zip):** extraia o ZIP inteiro e abra `YT-Chatterino.exe`. Mantenha as DLLs e pastas ao lado do executável. O runtime está incluído; não precisa instalar Python.
- **[Arch Linux — pacote nativo](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/download/v2.1.0/yt-chatterino-2.1.0-1-x86_64.pkg.tar.zst):** instale com `sudo pacman -U yt-chatterino-2.1.0-1-x86_64.pkg.tar.zst`. Veja as [instruções para Arch](docs/ARCH-LINUX.md).
- **[Todos os arquivos, notas e verificações SHA-256](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/tag/v2.1.0)**.

Os arquivos `README-PTBR.txt` e `README.txt`, incluídos no ZIP, explicam o uso em português e inglês.

## Conectar os chats e as contas

1. Clique em **+** e cole um link de transmissão do YouTube ou um `@canal` da Twitch.
2. Em **Configurações → Contas**, conecte a Twitch e autorize as permissões no navegador. A tela informa o que já está ativado; não é preciso autorizar de novo para atualizar a lista de emotes.
3. Para **enviar mensagens, moderar ou editar títulos no YouTube**, crie um cliente **OAuth para computador** no Google Cloud, ative a YouTube Data API v3 e escolha o JSON em Contas. Depois conecte a conta ou o canal de marca desejado.

**Ler chats do YouTube não exige chave de API nem login e não consome unidades da API oficial.** Envio e moderação usam a API oficial; a estimativa de unidades aparece ao focar um chat do YouTube. Para moderar ou editar uma transmissão, sua conta também precisa ter o cargo necessário no canal.

As contas ficam no armazenamento protegido do sistema. Sessões salvas são reutilizadas e renovadas quando possível. O **Tutorial** nas configurações reúne as etapas de conexão e as principais funções.

## Mensagens e mouse

| Ação | Como usar |
| --- | --- |
| Preparar uma resposta na Twitch | Clique direito na mensagem. O envio continua sendo feito por Enter ou pelo botão Enviar. |
| Abrir ações, cópia e moderação | **Ctrl + clique direito** na mensagem. |
| Rolar automaticamente | Clique com a **roda do mouse** no chat e mova o ponteiro para cima ou para baixo. Clique novamente ou pressione **Esc** para parar. |
| Ajustar a velocidade da roda | **Aparência → Velocidade da roda do mouse**. O padrão é 2×. |
| Abrir perfil de um usuário | Clique esquerdo no nome. |
| Abrir uma conversa de respostas | Clique no corpo de uma resposta da Twitch. |
| Completar emotes ou nomes | **Tab / Shift+Tab**. O botão **☺** abre o seletor. |

O seletor separa **Inscritos, Canal, Globais e Emojis**, filtra a aba atual e mostra nomes que podem ser usados nas mensagens. Emotes nativos, BTTV e 7TV são aceitos; os animados e GIFs recebidos são reproduzidos quando disponíveis. O envio de GIFs especiais da Twitch continua dependendo do chat oficial.

## Abas, painéis e aparência

- Use chats em abas separadas, divisões lado a lado/empilhadas ou um painel misto. Arraste as abas para reorganizar e as divisórias para redimensionar.
- O menu do canal agrupa ações do canal, pesquisa, filtros/histórico e layout. Ali também é possível abrir a transmissão, o player ou o chat no navegador e salvar canais.
- O limite padrão é **2.000 mensagens por painel**, ajustável nas configurações. Mensagens excedentes são descartadas.
- Ajuste fontes de mensagem e usuário, tamanho de emotes e emblemas, timestamps, datas, fundos alternados, destaques e sons.
- Os chats do YouTube continuam conectados nas abas em segundo plano. A pausa é manual. Alertas de início de transmissão são destinados à Twitch.
- A posição e o tamanho da janela são salvos. **Sempre no topo** está em Aparência. A interface pode ser usada em inglês ou português brasileiro.

Atalhos principais: **Ctrl+T** adiciona chats; **Ctrl+W** fecha o painel; **Ctrl+Shift+T** adiciona uma divisão; **Ctrl+F** pesquisa; **Ctrl+Shift+F** pesquisa em todos; **Ctrl+P** abre Aparência; **Ctrl+F5** reconecta. A lista completa fica em **Atalhos de teclado**.

## Desempenho: 1.3.0 → 2.1.0

A 1.3.0 usava Python/Tk; a 2.1.0 usa C++/Qt, desenha apenas as mensagens visíveis e mantém caches limitados.  
A 2.1.0 também melhora os nomes e o seletor de emotes, indica as permissões das contas e adiciona resposta direta e rolagem automática.

O [benchmark reproduzível](docs/BENCHMARK-1.3.0-2.1.0.md) compara inicialização, carregamento de 2.000 mensagens e 80 redimensionamentos, na mesma máquina e sem rede. Medianas: **inicialização 529 → 297 ms**, **histórico 872 → 191 ms** e **80 redimensionamentos 4.968 → 449 ms**. A 1.3.0 foi executada pelo código-fonte Python/Tk; rede e empacotamento foram excluídos. O relatório contém o protocolo, a variação e os dados brutos.

## Atualizar

Quando houver uma versão nova, aparece uma seta de download na barra superior. Clique nela, confira a versão e confirme **Instalar atualização e reiniciar**. As configurações e contas salvas são preservadas; o atualizador mantém uma cópia de recuperação.

Instalações antigas pelo código-fonte e o antigo pacote Arch baseado em Python precisam de uma atualização manual para a versão nativa. Nas versões nativas 2.0.0/2.0.1, um atalho `.lnk` dentro da pasta pode impedir a atualização: mova o atalho temporariamente para fora, atualize e coloque-o de volta. A preservação de atalhos foi corrigida na 2.0.2.

## Código e verificações

O código da aplicação está em `src/`; [instruções de compilação](docs/BUILDING.md), [verificação da versão](docs/VERIFICACAO-2.1.0.md) e [histórico de alterações](CHANGELOG.md) estão no repositório. Os testes usam perfis isolados e transportes simulados para envio, moderação e edição de canais.

Os avisos e licenças das dependências estão em `Licenses/`. Os fontes correspondentes às bibliotecas Qt distribuídas acompanham os arquivos da versão. As versões anteriores permanecem disponíveis nas tags do GitHub.

**As notas de versão e este README são mantidos em português brasileiro.**
