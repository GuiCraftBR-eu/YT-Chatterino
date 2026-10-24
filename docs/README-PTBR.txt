YT-CHATTERINO 2.0.2 — WINDOWS 11 x64

1. Baixe YT-Chatterino-Windows11-2.0.2.zip na versão publicada no GitHub.
2. Clique com o botão direito no ZIP e escolha Extrair Tudo. Abra a pasta YT-Chatterino extraída.
3. Dê dois cliques em YT-Chatterino.exe. Não precisa de Python, compilador, instalador nem Prompt de Comando.
4. Mantenha todas as DLLs e pastas de plugins junto do EXE. Não execute dentro do ZIP.

ADICIONAR UM CHAT
Pressione Ctrl+T (ou clique em +). Cole o link de uma LIVE do YouTube ou um @canal/link da Twitch.
A leitura pública do YouTube não precisa de chave de API, conta do Google nem unidades de cota.
Twitch: Configurações > Contas > Entrar na Twitch / permissões. Autorize o código no navegador uma vez.

ENVIAR / MODERAR
Digite abaixo do chat e pressione Enter. Clique com o botão direito em uma mensagem para responder/moderar.
A Twitch usa seu acesso salvo e solicita permissões adicionais quando necessário.
O envio e a moderação no YouTube exigem seu próprio JSON de cliente OAuth do Google para computador.
Ative a YouTube Data API v3 no projeto do Google Cloud; configure a tela de consentimento;
crie um cliente OAuth do tipo Aplicativo para computador; baixe o JSON. Em Contas, escolha
o JSON e clique em Entrar no YouTube. Se necessário, adicione sua conta como usuário de teste.
Você precisa ter permissão real de moderador/dono no canal. Há mais instruções no Tutorial.
Não compartilhe seu JSON, tokens nem perfil com amigos. Compartilhe somente este ZIP limpo.

CONTROLES ÚTEIS
Ctrl+Shift+T: adicionar painel. Ctrl+W: fechar painel focado. Ctrl+Shift+W: fechar aba.
Ctrl+Tab: próxima aba. Ctrl+1-8: selecionar aba. Ctrl+9: última aba.
Arraste as abas para reordenar; arraste as divisórias para redimensionar. Role sobre as abas para trocar.
Clique com o botão do meio no espaço vazio das abas para adicionar chat. Clique com o direito no cabeçalho para opções de layout/navegador.
Dê dois cliques no cabeçalho para ampliar/restaurar o painel. Alt+Setas: focar outro painel.
Ctrl+F / Ctrl+Shift+F: buscar no painel / em todos os chats. Ctrl+K: troca rápida.
Ctrl+F5: reconectar painel. Ctrl+P: configurações. Ctrl++ / Ctrl+- / Ctrl+0: tamanho do texto.
Tab / Shift+Tab: completar emotes ou @nomes. Clique no botão de sorriso para abrir o seletor de emotes.
Clique com o esquerdo em um nome para abrir o perfil; em uma resposta da Twitch para abrir a conversa retida.

CONFIGURAÇÕES
Aparência: idioma, fontes das mensagens/nomes, tamanho de distintivos/emotes, datas, horários e sempre no topo.
Mensagens: histórico e limite de mensagens (padrão: 2000 por painel; permitido: 100-20000).
Destaques: usuários, cargos e frases. Notificações: sons de menções e alertas de lives da Twitch.
Canais salvos: salvar/abrir canais. O YouTube só procura lives atuais ao abrir um canal salvo.
YouTube e Twitch continuam lendo nas abas em segundo plano. Pause o YouTube manualmente se quiser.
A posição, tamanho da janela, abas e painéis são salvos. Os acessos são criptografados para seu usuário do sistema.
A busca nas configurações e o Tutorial explicam os demais recursos.

ATUALIZAÇÕES
A seta para baixo aparece quando há uma versão nova. Clique nela e confirme.
O aplicativo verifica o arquivo, faz backup da pasta, instala e reinicia. Se a nova versão
não iniciar, a anterior é restaurada. Mantenha a pasta extraída gravável e feche outras cópias antes de atualizar.
A versão portátil 1.x para Windows pode atualizar para esta versão. Instalações pelo código-fonte precisam de uma atualização manual pelo ZIP.
Se a versão nativa 2.0.0/2.0.1 rejeitar um atalho .lnk durante a atualização, mova esse atalho para fora da pasta do aplicativo,
tente atualizar novamente e depois coloque o atalho de volta. A versão 2.0.2 corrige a preservação de atalhos nas próximas atualizações.
Os arquivos Source/*.py* são marcadores de compatibilidade não utilizados, exigidos pelo atualizador antigo;
não são código Python executado pelo aplicativo. Todo o aplicativo e seu ambiente são nativos em C++/Qt.

LIMITES / AJUDA
As unidades de ações do YouTube são uma estimativa; a leitura usa zero unidades da API. Aplicativos do Google
em teste podem exigir novo acesso após sete dias. O histórico recente da Twitch usa um serviço público externo
e pode ficar indisponível. As conversas incluem somente mensagens retidas. GIFs recebidos da Twitch
são exibidos; o envio de GIFs é feito pelo navegador da Twitch. Mudanças no site do YouTube podem interromper a leitura.
Esta versão não possui assinatura digital: o Windows pode mostrar um aviso de editor desconhecido. Não desative
suas ferramentas de segurança. Baixe somente em https://github.com/GuiCraftBR-eu/YT-Chatterino/releases
Os avisos das bibliotecas ficam em Licenses. O código-fonte e as instruções de compilação estão no repositório do GitHub.
