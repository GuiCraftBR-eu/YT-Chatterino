<div align="center">

# YT-Chatterino

**Seus chats ao vivo, em uma só janela.**

Cliente independente para chats do YouTube e da Twitch, com abas, painéis e emotes.

**Windows 11 · Arch Linux · C++20 / Qt 6 · PT-BR / English**

[Baixar v2.2.0](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/tag/v2.2.0) · [Como usar](#primeiros-passos) · [Compilar](docs/BUILDING.md) · [Aviso de independência](#independência-e-marcas)

</div>

> [!IMPORTANT]
> **Projeto independente e não oficial.** O YT-Chatterino não é afiliado, associado, patrocinado, aprovado nem mantido pelo YouTube/Google, pela Twitch/Amazon ou pelo projeto Chatterino e seus responsáveis. Não é uma versão oficial do Chatterino. Os nomes são mencionados apenas para identificar compatibilidade e a inspiração da interface.

## Baixar e abrir

| Sistema | Download da versão 2.2.0 | Instalação |
| --- | --- | --- |
| **Windows 11** | [Baixar ZIP](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/download/v2.2.0/YT-Chatterino-Windows11-2.2.0.zip) | Extraia tudo e abra `YT-Chatterino.exe`. Mantenha as DLLs e pastas junto do executável. |
| **Arch Linux** | [Baixar pacote nativo](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/download/v2.2.0/yt-chatterino-2.2.0-1-x86_64.pkg.tar.zst) | Instale com o comando abaixo. [Guia para Arch](docs/ARCH-LINUX.md). |

```sh
sudo pacman -U ./yt-chatterino-2.2.0-1-x86_64.pkg.tar.zst
```

Não precisa de Python. Baixe pelo repositório oficial **deste projeto**; os arquivos e as verificações SHA-256 estão na [página da versão](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/tag/v2.2.0).

## Primeiros passos

1. Clique em **+** ou pressione **Ctrl+T**. Cole o link de uma transmissão do YouTube ou um `@canal` da Twitch.
2. Para conectar a Twitch, abra **Configurações → Contas** e autorize o acesso no navegador.
3. Para enviar mensagens, moderar ou editar títulos no YouTube, ative a **YouTube Data API v3** no Google Cloud, crie um cliente **OAuth para computador** e selecione o JSON em **Contas**. Depois, conecte sua conta.

A leitura pública do YouTube não exige login nem chave de API e não usa unidades da API oficial. Envio e moderação usam a API oficial e suas cotas; moderar ou editar uma transmissão exige as permissões correspondentes no canal. Se o YouTube retornar HTTP 429, você pode ativar por chat a alternativa pela API oficial no menu do canal. Ela exige a conta conectada, consome 5 unidades por solicitação e usa aproximadamente 986 unidades por hora por chat no intervalo padrão de 18,25 segundos; vários chats consomem mais. A cota exibida é uma estimativa, e a API também pode impor limites ou negar acesso.

O **Tutorial**, nas configurações, explica as conexões e os recursos. Para conectar o YouTube, abra o **guia com imagens** em Configurações → Contas ou no tutorial. As credenciais usam o armazenamento protegido do sistema; não compartilhe tokens, perfis ou seu JSON OAuth. No Arch, salvar acessos exige um [chaveiro compatível](docs/ARCH-LINUX.md).

**Google bloqueou o login com 403 / `access_denied`?** Se o projeto estiver em teste, seu responsável precisa adicionar a conta Google exata em **Google Auth Platform → Público-alvo → Usuários de teste**. Depois, cancele o login pendente e tente novamente. Veja o [guia de solução](docs/GOOGLE-LOGIN.md), incluindo a opção de usar seu próprio projeto.

## O que você pode fazer

- **Organizar os chats:** abas, painéis lado a lado ou empilhados e painel misto.
- **Conversar:** respostas na Twitch, preenchimento de nomes e seletor de emotes nativos, BTTV e 7TV.
- **Acompanhar mensagens:** pesquisa, filtros, destaques e sons; histórico padrão de 2.000 mensagens por painel.
- **Personalizar:** fontes, emotes, emblemas, cores, horários e janela sempre no topo.
- **Moderar:** ações conforme as permissões da sua conta e os recursos de cada plataforma.

### Controles essenciais

| Ação | Controle |
| --- | --- |
| Adicionar chat / painel | **Ctrl+T** / **Ctrl+Shift+T** |
| Fechar painel | **Ctrl+W** |
| Pesquisar no painel / em todos | **Ctrl+F** / **Ctrl+Shift+F** |
| Preparar resposta na Twitch | Clique direito na mensagem; **Enter** para enviar |
| Abrir ações e moderação | **Ctrl + clique direito** na mensagem |
| Completar emotes ou nomes | **Tab / Shift+Tab** |
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
