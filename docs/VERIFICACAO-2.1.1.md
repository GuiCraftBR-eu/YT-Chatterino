# Verificação da versão 2.1.1

A suíte nativa no Windows executou **102 casos/linhas de dados**, sem falhas ou testes ignorados, com Qt 6.8.3 e MinGW 13.1. Os testes usam perfis isolados e transportes simulados para envio, moderação e alterações de canal. Nenhuma mensagem ou ação de moderação real é necessária para executá-los.

O trabalho da [PR #1](https://github.com/GuiCraftBR-eu/YT-Chatterino/pull/1) foi mantido e verificado: a suíte cobre recusa `access_denied`, outros erros de autorização, estado OAuth incorreto, limpeza do login pendente, preservação do acesso anterior e ajuda em português. A [PR #2](https://github.com/GuiCraftBR-eu/YT-Chatterino/pull/2) forneceu os ajustes de interface e do tutorial desta versão.

As novas verificações cobrem a abertura do resultado correto após mover uma aba, a mensagem removida do histórico, botões para limpar buscas e o texto obrigatório de usuários de teste em inglês e português. O tutorial também foi renderizado e inspecionado em uma janela de teste com perfil isolado.

O pacote Windows foi gerado com Qt, seus plugins, licenças e manifesto; o EXE iniciou com suas próprias DLLs e informou a versão 2.1.1. Uma cópia isolada da versão 2.1.0 recebeu a atualização por meio do ajudante real de atualização. A versão nova confirmou a inicialização, preservou arquivos pessoais de teste e uma cópia de recuperação, e removeu os arquivos temporários. A suíte também verifica falha/restauração e preservação de atalhos no Windows. Este exercício não modificou uma instalação pessoal nem testou o download da atualização pela interface.

O fluxo de publicação compila e testa Windows e Arch antes de criar a versão. Ele valida os nomes, conteúdo e SHA-256 dos pacotes e dos fontes da biblioteca Qt; a publicação ocorre somente depois de conferir os arquivos anexados ao rascunho no GitHub. Os testes de integração com serviços reais e contas dependem de rede e das permissões das contas.

A leitura real de uma live do YouTube foi tentada, mas o Google respondeu com HTTP 429 e uma página de verificação. Não foi possível confirmar mensagens ao vivo nessa rede; a nova regressão verifica a mensagem clara e a espera de dois minutos sem expor a URL de redirecionamento.
