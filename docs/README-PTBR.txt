YT-CHATTERINO 2.5.0 — WINDOWS 11 x64

1. Baixe YT-Chatterino-Windows11-2.5.0.zip na versão publicada no GitHub.
2. Clique com o botão direito no ZIP e escolha Extrair Tudo. Abra a pasta YT-Chatterino extraída.
3. Dê dois cliques em YT-Chatterino.exe. Não precisa de Python, compilador, instalador nem Prompt de Comando.
4. Mantenha todas as DLLs e pastas de plugins junto do EXE. Não execute dentro do ZIP.

ADICIONAR UM CHAT
Pressione Ctrl+T (ou clique em +). Digite o canal ou cole o link. No YouTube, use o identificador do link: por exemplo, LofiGirl para youtube.com/@LofiGirl. O @ é opcional para abrir e salvar canais.
A leitura comum pelo site do YouTube não precisa de chave, conta do Google nem unidades de cota.
Se o Google limitar o acesso (HTTP 429), conecte sua conta e ative por chat a alternativa
da API oficial no menu do canal. Ela usa 5 unidades por solicitação, cerca de 986 unidades
por hora por chat no intervalo padrão de 18,25 segundos; vários chats consomem mais.
Twitch: Configurações > Contas > Conectar Twitch. Autorize o código no navegador uma vez.
A tela informa a conta conectada e as permissões ativadas; não é preciso autorizar novamente para atualizar emotes.

KICK: CONECTAR, ENVIAR E MODERAR
Para ler, abra o link ou selecione Kick e digite o canal; não é preciso entrar.
1. Abra o guia ilustrado em Configurações > Tutorial > Contas > Kick.
2. Crie seu aplicativo em https://kick.com/settings/developer e cadastre exatamente
   http://localhost:37365/callback como endereço de redirecionamento.
3. Selecione Read user information e Write to Chat feed. Para moderação e edição,
   selecione também Read channel information, Update channel information, Execute
   moderation actions for moderators e Execute moderation actions on chat messages.
4. Em Configurações > Contas > Kick, informe o ID e o segredo e autorize no navegador.
   Reconecte depois de alterar permissões; desmarque gerenciamento se quiser só enviar.
O segredo é salvo no armazenamento protegido do sistema; nunca o compartilhe.
O chat ao vivo usa conexão pública não oficial da Kick e recupera histórico recente
após desconexões. Mudanças no site ou falhas prolongadas ainda podem causar perdas.
Ações exigem o cargo no canal; editar título/categoria exige a conta do dono.

ENVIAR / MODERAR
Digite abaixo do chat e pressione Enter. Clique direito na mensagem da Twitch para preparar uma resposta. Ctrl + clique direito abre o menu de ações/moderação.
A Twitch usa seu acesso salvo e solicita permissões adicionais quando necessário.
O envio e a moderação no YouTube exigem seu próprio JSON de cliente OAuth do Google para computador.
Ative a YouTube Data API v3 no projeto do Google Cloud; configure a tela de consentimento;
crie um cliente OAuth do tipo Aplicativo para computador; baixe o JSON. Em Contas, escolha
o JSON e clique em Entrar no YouTube. O guia com imagens fica em Configurações > Contas. Se o app OAuth estiver em teste, adicione obrigatoriamente a conta Google exata usada no login em Público-alvo > Usuários de teste.
Você precisa ter permissão real de moderador/dono no canal. Há mais instruções no Tutorial.
Não compartilhe seu JSON, tokens nem perfil com amigos. Compartilhe somente este ZIP limpo.

REPLAY DE CHAT DE VOD
1. No app, clique em + > Configurar extensão de VOD e abra a pasta da extensão.
2. No Chrome/Edge, abra Extensões, ative o modo de desenvolvedor e use Carregar sem
   compactação para selecionar a pasta browser-extension. Para Firefox, baixe o XPI assinado
   na release do GitHub; em about:addons, clique na engrenagem > Instalar extensão de arquivo.
   Instale essa versão uma vez para receber automaticamente as próximas versões assinadas.
3. Abra e reproduza uma VOD compatível. No app, clique em +, selecione a VOD detectada
   e clique em Abrir. Ao fechar o chat, a VOD ainda pode ser selecionada enquanto o vídeo
   estiver aberto. Pause ou avance o vídeo e o chat acompanha. Para misturar,
   abra as VODs no navegador, clique em +, escolha Chat misturado e marque as VODs abertas.
   Também é possível selecionar Link de VOD e adicionar links manualmente. Na primeira
   atualização a partir da versão anterior, carregue novamente a pasta da extensão atualizada.
O replay é somente leitura e depende de a plataforma ter guardado e disponibilizado o chat.
Usa interfaces públicas não oficiais e pode falhar, ser limitado ou omitir mensagens/emotes.
A extensão envia os endereços de páginas de vídeo do YouTube e VODs da Twitch/Kick e o
estado da reprodução ao app local, para identificar e sincronizar replays.

ABRIR NO CELULAR
Clique com o direito na aba, escolha o painel e Abrir no celular. Selecione
a rede local, inicie e leia o QR code no celular ou digite o endereço e código exibidos.
Use a mesma rede local no celular e no computador (o PC pode usar cabo). Deixe o
aplicativo aberto. A janela do QR code pode ser fechada; clique em Celular no painel
para reabri-la e em Parar para encerrar o acesso. Se o Windows
solicitar acesso pela rede, permita apenas redes privadas; o firewall pode bloquear
a conexão até ser configurado. A conexão usa HTTP local: use uma rede confiável. A versão para celular permite ler, enviar, responder, usar emotes e moderar conforme
suas permissões, além de editar título e categoria. Toque em emotes e emblemas para ampliá-los.

CONTROLES ÚTEIS
Ctrl+Shift+T: adicionar painel. Ctrl+W: fechar painel focado. Ctrl+Shift+W: fechar aba.
Ctrl+Tab: próxima aba. Ctrl+1-8: selecionar aba. Ctrl+9: última aba.
Arraste o título da live para ajustar a altura. Arraste as abas para reordenar; arraste as divisórias para redimensionar. Role sobre as abas para trocar.
Clique com o botão do meio no espaço vazio das abas para adicionar chat. Clique com o direito no cabeçalho para opções de layout/navegador.
Dê dois cliques no cabeçalho para ampliar/restaurar o painel. Alt+Setas: focar outro painel.
Ctrl+F / Ctrl+Shift+F: buscar no painel / em todos os chats. Ctrl+K: troca rápida.
Ctrl+F5: reconectar painel. Ctrl+P: configurações. Ctrl++ / Ctrl+- / Ctrl+0: tamanho do texto.
Tab / Shift+Tab: completar emotes ou @nomes. Clique no botão de sorriso para abrir o seletor de emotes.
No campo de mensagem, use as setas para cima e para baixo para percorrer as mensagens enviadas nesta sessão.
Clique em uma menção @usuário para ver o cartão e o histórico disponível desse usuário no canal.
Clique com a roda no chat e mova o ponteiro para rolar automaticamente. Clique novamente ou use Esc para parar.
A roda física usa velocidade 2× por padrão; ajuste em Aparência. O touchpad mantém a rolagem em pixels.
No seletor, Inscritos, Canal, Globais e Emojis têm listas separadas. A busca filtra a aba atual.
Clique com o esquerdo em um nome para abrir o perfil; em uma resposta da Twitch para abrir a conversa retida.
O cartão pode mostrar desde quando a pessoa segue o canal: na Twitch, para outros usuários,
é necessário ser dono ou moderador e autorizar a leitura de seguidores; para a própria
conta é solicitada outra permissão. No YouTube, é necessário entrar e a inscrição precisa
ser pública. A API pública da Kick não informa essa data.

CONFIGURAÇÕES
Aparência: idioma, fontes das mensagens/nomes, tamanho de distintivos/emotes, velocidade da roda, datas, horários e sempre no topo.
Mensagens: histórico, limite de mensagens (padrão: 2000 por painel; permitido: 100-20000) e pausa de rolagem ao passar o mouse, indicada discretamente no chat.
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
As unidades do YouTube são uma estimativa; a leitura via site usa zero unidades da API.
A alternativa opcional pela API oficial consome cota. Aplicativos do Google
em teste podem exigir novo acesso após sete dias. O histórico recente da Twitch usa um serviço público externo
e pode ficar indisponível. As conversas incluem somente mensagens retidas. GIFs recebidos da Twitch
são exibidos; o envio de GIFs é feito pelo navegador da Twitch. Mudanças no site do YouTube podem interromper a leitura.
Esta versão não possui assinatura digital: o Windows pode mostrar um aviso de editor desconhecido. Não desative
suas ferramentas de segurança. Baixe somente em https://github.com/GuiCraftBR-eu/YT-Chatterino/releases
Os avisos das bibliotecas ficam em Licenses. O código-fonte e as instruções de compilação estão no repositório do GitHub.

GOOGLE: ACESSO BLOQUEADO (403 / access_denied)
Se o navegador disser que o app está em teste, o responsável pelo projeto do JSON
OAuth selecionado deve abrir Google Auth Platform > Público-alvo > Usuários de teste,
adicionar a conta Google exata usada no login e salvar. Em Contas, cancele o login
pendente e tente novamente. Você também pode configurar seu próprio projeto/JSON.
Reinstalar o aplicativo não altera a lista de testadores. Para uso público do mesmo
cliente OAuth, siga os requisitos de publicação/verificação do Google.
Guia: https://github.com/GuiCraftBR-eu/YT-Chatterino/blob/main/docs/GOOGLE-LOGIN.md
