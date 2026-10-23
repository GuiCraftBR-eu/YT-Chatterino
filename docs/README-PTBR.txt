YT-CHATTERINO - EDIÇÃO PORTÁTIL PARA WINDOWS 11
Atualizado em: 23 de outubro de 2026

INÍCIO RÁPIDO
=============
1. Clique com o botão direito em YT-Chatterino-Windows11-1.2.2.zip > Extrair Tudo.
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
Clique na engrenagem ou pressione Ctrl+P. Em Aparência > Exibição > Idioma / Language,
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
- Clique com o botão direito em um usuário para destacá-lo. Aparência > Destaques
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
- Canais offline da Twitch também podem carregar mensagens recentes disponíveis no serviço de histórico.
- Sem som: confira Notificações e o volume do Windows.
Aplicativo independente; não é uma versão oficial da Twitch, YouTube ou Chatterino.

VERSÃO 1.2.0: RESPOSTAS, CARTÕES DE USUÁRIO E LIMITES
==================================================
Twitch: clique com o botão direito em uma mensagem > Responder à mensagem.
O destinatário aparece acima do campo de envio. Digite e envie normalmente;
Esc ou x cancela a resposta. As respostas recebidas mostram o contexto original.

Clique com o botão esquerdo no nome de um usuário da Twitch ou do YouTube para
abrir seu cartão. Ele mostra os dados públicos fornecidos pela plataforma,
emblemas e meses de inscrição informados no chat, além de até 500 mensagens
desse usuário ainda mantidas no canal. Há botões de perfil, destaque, menção
e moderação. As permissões da plataforma continuam necessárias; informações
privadas de inscrição não são públicas.
Os detalhes adicionais de perfis do YouTube usam a conta Google conectada e
custam 1 unidade de API por perfil ainda não consultado. Sem login, os dados
básicos do chat continuam disponíveis.

Configurações > Aparência > Mensagens: o limite padrão é de 2000 mensagens por painel.
Escolha um valor entre 100 e 20000. Para alterar um painel específico, clique
com o botão direito no cabeçalho > Limite de mensagens. Um chat misturado divide
seu limite entre os canais. Textos, imagens e referências de renderização
antigos são removidos. Isso não é um registro permanente de conversas.

A Twitch carrega até 200 mensagens recentes do serviço público independente
recent-messages.robotty.de, também usado pelo Chatterino. Ele recebe o nome do
canal, nunca os tokens da conta. A disponibilidade varia, principalmente em
canais recém-acompanhados. Desative em Configurações > Aparência > Mensagens, se desejar.
O YouTube carrega apenas as mensagens recentes fornecidas pela própria página.
Mensagens históricas não acionam sons.

DOWNLOAD NATIVO PARA ARCH LINUX
==============================
Há um pacote separado de Arch Linux na página da versão no GitHub. Instale
yt-chatterino-1.2.2-1-any.pkg.tar.zst com pacman -U, ou compile o PKGBUILD incluído
no código-fonte .tar.gz com makepkg -si. Não usa Wine. Consulte o arquivo
docs/ARCH-LINUX.md desse download para dependências, chaveiro seguro e sons.

VERSÃO 1.2.1: CONFIGURAÇÕES E REDIMENSIONAMENTO
==============================================
Aparência agora reúne Exibição, Mensagens e Destaques. Use Pesquisar
configurações no topo da janela de configurações ou Ctrl+F nessa janela.
Busque nomes ou palavras-chave em inglês ou português. Enter abre o primeiro
resultado; Seta para baixo seleciona resultados; clique duplo ou Abrir
configuração leva ao controle. Esc limpa a busca. Trocar de página mantém
os rascunhos não aplicados até a janela ser fechada.

Redimensionar a janela e os painéis evita recalcular repetidamente todo o
histórico mantido. Durante o redimensionamento, as mensagens visíveis são
renderizadas separadamente; novas atualizações de exibição e quadros animados
voltam quando o arrasto termina. O histórico, a seleção e a posição de rolagem
são preservados. Consulte docs/PERFORMANCE.md no download do código-fonte
para a medição reproduzível e suas limitações.

VERSÃO 1.2.2: ATUALIZAÇÕES, DATAS E HORÁRIOS
===========================================
O aplicativo verifica versões estáveis do GitHub ao iniciar e a cada seis horas.
Quando um pacote mais recente está pronto, aparece uma seta de download ao lado
da área do saldo do YouTube. Clique nela e depois em Instalar atualização e
reiniciar. O aplicativo baixa, confere a integridade pelo código SHA-256, fecha,
substitui seus arquivos e reinicia. Contas, configurações e arquivos adicionais
na pasta são preservados. Um backup dos arquivos anteriores fica ao lado da
pasta do aplicativo. Se a versão portátil ou de código-fonte falhar ao iniciar,
os arquivos anteriores são restaurados. Feche outras cópias antes de atualizar.

Configurações > Atualizações permite desativar verificações em segundo plano ou
verificar manualmente. Instalações pelo pacman no Arch usam pkexec/polkit para
autorizar o pacman; instale o polkit e um agente de autenticação para esse fluxo.
O aplicativo continua sendo executado pelo seu usuário. Atualizações do
código-fonte mantêm o Python e as dependências já instaladas. Versões portáteis
e de código-fonte precisam de permissão de escrita na pasta do aplicativo.
Versões 1.2.1 e anteriores exigem uma atualização manual para a 1.2.2 primeiro.

Aparência > Exibição agora tem Formato de data (padrão DD/MM/YYYY, ou seja,
dia/mês/ano) e Formato do horário: HH:mm, HH:mm:ss, hh:mm AM/PM ou hh:mm:ss AM/PM.
Clique em Aplicar para atualizar as mensagens já mantidas, inclusive horários
ocultos. Datas dos cartões de usuário e dos avisos de atualização seguem o
formato escolhido. Textos de mensagens e formatos da API e do armazenamento
continuam intactos.

O saldo do YouTube aparece apenas quando o painel focado inclui o YouTube.
A seta de atualização continua disponível ao ver a Twitch. A inicialização adia
trabalho de tradução e descompactação até ele ser necessário; a preparação dos
padrões de emotes ocorre fora da interface.
