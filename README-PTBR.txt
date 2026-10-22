YT-CHATTERINO - EDIÇÃO PORTÁTIL PARA WINDOWS 11
Atualizado em: 22 de outubro de 2026

INÍCIO RÁPIDO
=============
1. Clique com o botão direito em YT-Chatterino-Windows11-Updated.zip > Extrair Tudo.
2. Abra a pasta YT-Chatterino extraída.
3. Dê dois cliques em YT-Chatterino.exe.

O Python está incluído. Não é necessário instalar Python, usar pip, executar
como administrador ou usar um instalador. Mantenha _internal ao lado do
executável. Não abra dentro do ZIP nem copie somente o executável. Você pode
criar um atalho para ele na área de trabalho. A versão é para Windows 11 x64
(Intel/AMD). Computadores ARM podem usar emulação x64, mas ARM não foi testado.
O modo S e computadores gerenciados podem restringir aplicativos portáteis.
O executável não é assinado; não desative a segurança para executá-lo.

IDIOMA, CONFIGURAÇÕES E AJUDA
============================
Clique na engrenagem ou pressione Ctrl+P. Em Aparência > Idioma / Language,
escolha English ou Português (Brasil). A alteração é imediata e fica salva.
Menus, botões, ajuda e mensagens de conexão seguem o idioma escolhido;
mensagens do chat, nomes de usuários, títulos e códigos de emotes não mudam.
Aparência inclui Sempre no topo, horários, fundos alternados, espaçamento
compacto e alturas separadas para distintivos e emotes. Configurações >
Tutorial tem guias curtos sobre conexões, moderação e outros recursos.

TWITCH: CONECTAR, ENVIAR E MODERAR
=================================
1. Configurações > Contas > Login / permissões da Twitch.
2. Aprove o código ou a página no navegador com SUA PRÓPRIA conta da Twitch.
3. Pressione Ctrl+T ou clique em + e insira @nome_do_canal ou um link da Twitch.
4. Digite abaixo do chat e pressione Enter ou Enviar.

O identificador público do aplicativo está incluído. Não é necessário criar
projeto de desenvolvedor nem obter token separado. O login é salvo com
segurança para seu usuário do Windows. Permissões adicionais podem ser
solicitadas para enviar, moderar ou carregar emotes de inscritos.
BTTV e 7TV não exigem login separado.
Clique com o botão direito em uma mensagem > Moderação para excluir,
suspender ou banir. Sua conta deve ser dona ou moderadora do canal. A API
da Twitch não exclui mensagens do próprio dono; remova-as na Twitch.
Clique com o botão direito no cabeçalho > Ações do canal para editar título
ou categoria. A Twitch exige a conta do dono para editar; ser moderador ou
editor, por si só, não concede essa permissão da API.

YOUTUBE: PARA LER, BASTA O LINK DA LIVE
======================================
Ctrl+T > cole o link do VÍDEO AO VIVO. Ler chats públicos, carregar dados da
transmissão e abrir canais salvos não exige chave de API nem login do Google
e usa zero unidades da API. A opção de leitor pela API oficial foi removida.
Alterações, intervalos e restrições do site do YouTube ainda se aplicam.

YOUTUBE: ENVIAR, MODERAR E EDITAR
================================
Essas ações exigem OAuth do Google e usam a cota de API do seu projeto:
1. No Google Cloud, crie um projeto e ative YouTube Data API v3.
2. Configure o Google Auth Platform. Se o projeto estiver em teste, adicione
   sua conta do Google como usuário de teste.
3. Crie um cliente OAuth do tipo App para computador e baixe o JSON.
4. Configurações > Contas > Selecionar JSON OAuth do Google > Entrar no
   YouTube. Selecione o canal que você usa para conversar ou moderar.
5. Digite para enviar; clique com o botão direito em uma mensagem > Moderação
   para agir, ou no cabeçalho > Ações do canal para editar o título da live.

O Google verifica as permissões. Fazer login não torna você moderador.
Projetos em teste podem exigir novo login após sete dias. Cada amigo usa
seu PRÓPRIO projeto, JSON e contas. O ZIP não inclui credenciais nem a conta
de quem o enviou.
Guia oficial: https://developers.google.com/youtube/v3/guides/auth/installed-apps?hl=pt-br

ESTIMATIVA DE COTA DO YOUTUBE
============================
Clique no saldo YT acima dos chats ou em Configurações > Cota do YouTube.
A tela estima unidades restantes e quantas mensagens, exclusões ou ações de
banimento usariam esse saldo. São ALTERNATIVAS de uso do MESMO saldo;
não some as contagens de ações.
- Enviar: 20 unidades; a primeira ação pode consultar o ID do chat.
- Excluir: 20 unidades; mensagens do site podem exigir mais 5 a 6 de consulta.
- Suspender / banir / desfazer banimento: 200 unidades, mais consulta inicial
  se necessária.
- Editar título: cerca de 51 unidades, incluindo a consulta do título atual.
O limite padrão é de 10.000 unidades/dia. Altere-o se o Google concedeu outro
limite. Informe o uso de outros aplicativos do mesmo projeto para ajustar
a estimativa. O uso é salvo após cada ação. A cota é renovada à meia-noite
do horário do Pacífico; o ajuste de uso externo também é renovado nesse dia.
Esta é uma estimativa LOCAL: o aplicativo não vê o saldo real do Google.
Outros aplicativos/pessoas, falhas e consultas podem afetá-lo.
Custos: https://developers.google.com/youtube/v3/determine_quota_cost?hl=pt-br

RECURSOS E CONTROLES DO DIA A DIA
================================
- Ctrl+T adiciona chats; escolha abas, lado a lado, empilhados ou misturados.
  Clique com o botão do meio no espaço vazio das abas para adicionar chats.
  Ctrl+Shift+T adiciona uma divisão.
- Arraste abas para reordenar; role sobre elas para alternar. Arraste
  divisórias para redimensionar. Dê dois cliques no cabeçalho para ampliar
  ou restaurar o painel. Clique com o botão direito para mais ações.
- Clique com o botão direito em uma aba para abrir transmissão, player ou
  chat no navegador. O player do YouTube usa um servidor local com o app aberto.
- Em chats misturados, escolha o destino ao lado da caixa de mensagem.
  O contador mostra o limite: YouTube, 200; Twitch, 500 caracteres.
- Clique na carinha para abrir Inscritos / Canal / Globais / Emojis. Tab e
  Shift+Tab completam e alternam emotes ou @nomes. Emotes nativos, distintivos,
  BTTV/7TV, emotes animados e GIFs de inscritos recebidos da Twitch aparecem
  quando compatíveis. Envie GIFs pelo chat oficial da Twitch no navegador.
- Clique com o botão direito em um usuário para destacá-lo. Destaques
  adiciona cargos e palavras; Notificações controla sons de menção e alertas
  de live da Twitch. Cor padrão: #227400. Som padrão: ChatternoPing.wav.
- Canais abertos da Twitch recebem alertas de live por padrão. A primeira
  detecção é silenciosa. O YouTube não procura lives em segundo plano nem
  envia alertas de início de transmissão.
- Salve canais em Ações do canal; abra-os em Configurações > Canais salvos.
  O YouTube só procura lives atuais ao clicar em Abrir e permite escolher
  entre transmissões simultâneas. A Twitch também abre offline.
- Chats ocultos do YouTube pausam automaticamente. A Twitch permanece
  conectada. Pause cada chat do YouTube no menu do cabeçalho, ou todos pelo
  menu do botão direito na engrenagem. Os intervalos individuais respeitam
  o tempo recomendado pelo YouTube.
- Ctrl+F busca no painel; Ctrl+Shift+F busca em todos. Ctrl+F5 reconecta.
  Ctrl+Tab / Ctrl+Shift+Tab alterna abas. Alt+Setas troca o foco do painel.
  Ctrl++ / Ctrl+- / Ctrl+0 altera/restaura o texto. Ctrl+K troca de chat.
  Configurações > Atalhos de teclado lista todos os controles.
- Layouts, preferências, contas e canais salvos persistem ao reiniciar.

COMPARTILHAMENTO, DADOS PESSOAIS E SOLUÇÃO DE PROBLEMAS
=====================================================
Compartilhe este ZIP, não a pasta de configurações pessoais. Ele inclui o
executável, o ambiente _internal, o código-fonte em Source, licenças em
Licenses e os arquivos README.txt e README-PTBR.txt. Não inclui tokens,
contas, JSON do Google, chaves de API, configurações pessoais, layouts,
diagnósticos ou histórico do chat. Cada amigo usa suas próprias contas.
Credenciais usam o Gerenciador de Credenciais ou arquivos criptografados
pelo Windows. Configurações/diagnósticos usam arquivos .ytchatterino* em
%USERPROFILE%. Apagar o programa não apaga configurações ou logins salvos.
- O app não abre: extraia o ZIP inteiro novamente; mantenha _internal intacta.
- A Twitch falha: confira a conexão, reconecte ou entre novamente em Contas.
- A leitura do YouTube falha: confira o link da live e se o chat está ativo.
  Alterações/restrições do site podem interromper a leitura; use Abrir chat
  no navegador.
- As ações do YouTube falham: confira ativação da API, OAuth, usuários de
  teste, conta/canal selecionado, direitos de moderação e cota do projeto.
- Chats offline da Twitch recebem apenas mensagens novas, sem baixar histórico.
- Sem som: confira Notificações e o volume do Windows.
Aplicativo independente; não é uma versão oficial da Twitch, YouTube ou Chatterino.
