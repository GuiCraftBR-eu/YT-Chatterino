PT_BR_CHAT = {'Messages': 'Mensagens', 'Message limit': 'Limite de mensagens', 'Message limit…': 'Limite de mensagens…', 'Retained messages in this pane (100–20000):': 'Mensagens mantidas neste painel (100–20000):', 'Default message limit per pane': 'Limite padrão de mensagens por painel', 'Load recent Twitch messages when opening a channel': 'Carregar mensagens recentes da Twitch ao abrir um canal', 'Twitch history uses recent-messages.robotty.de without account tokens. Availability varies. YouTube loads the recent messages its page provides.': 'O histórico da Twitch usa recent-messages.robotty.de sem tokens da conta. A disponibilidade varia. O YouTube carrega as mensagens recentes fornecidas pela página.', 'Apply message limits / history': 'Aplicar limites / histórico', 'Use a message limit from 100 to 20000.': 'Use um limite de 100 a 20000 mensagens.', 'Reply to message': 'Responder à mensagem', 'User information': 'Informações do usuário', 'Replying to @{name}: {body}': 'Respondendo a @{name}: {body}', '↪ Replying to @{name}: {body}': '↪ Respondendo a @{name}: {body}', 'Recent Twitch history unavailable; live chat is connected independently.': 'Histórico recente da Twitch indisponível; a conexão do chat ao vivo é independente.', 'No recent Twitch messages available for this channel.': 'Não há mensagens recentes da Twitch disponíveis para este canal.', 'Loading public profile…': 'Carregando perfil público…', 'ID: {id}': 'ID: {id}', 'Subscribed for {months} months': 'Inscrito há {months} meses', 'No badges reported in this chat': 'Nenhum emblema informado neste chat', 'Open profile': 'Abrir perfil', 'Highlight': 'Destacar', 'Unban': 'Desbanir', 'Ban': 'Banir', 'Recent messages retained in this channel': 'Mensagens recentes mantidas neste canal', 'Created: {date}': 'Criado em: {date}', 'Followers: {count}': 'Seguidores: {count}', 'Subscribers: {count}': 'Inscritos: {count}', 'Connect YouTube to load public profile details (1 API unit).': 'Conecte o YouTube para carregar os detalhes públicos do perfil (1 unidade de API).', 'Public details unavailable': 'Detalhes públicos indisponíveis', '{count} retained messages · up to 500 shown': '{count} mensagens mantidas · até 500 exibidas', 'Unlock a Secret Service / KWallet keyring to save sign-in securely.': 'Desbloqueie um chaveiro Secret Service / KWallet para salvar o login com segurança.'}


# v1.2.1 settings navigation.
PT_BR_CHAT['1 setting found'] = '1 configuração encontrada'
PT_BR_CHAT.update({'Display': 'Exibição', 'Search settings': 'Pesquisar configurações', 'Open setting': 'Abrir configuração', '{count} settings found': '{count} configurações encontradas', 'No settings found. Try another word.': 'Nenhuma configuração encontrada. Tente outra palavra.'})

# v1.2.2 date/time preferences and update flow.
PT_BR_CHAT.update({
    'Updates': 'Atualizações',
    'App updates': 'Atualizações do aplicativo',
    'Date format': 'Formato de data',
    'Timestamp format': 'Formato do horário',
    'HH = 24-hour; hh with AM/PM = 12-hour. Add :ss to show seconds.': 'HH = 24 horas; hh com AM/PM = 12 horas. Adicione :ss para mostrar segundos.',
    'Check for updates automatically': 'Verificar atualizações automaticamente',
    'Installed version: {version}': 'Versão instalada: {version}',
    'Installing closes and restarts the app. Your accounts and settings are kept.': 'A instalação fecha e reinicia o aplicativo. Suas contas e configurações são mantidas.',
    'Arch Linux updates use pacman and may ask for administrator authorization.': 'Atualizações no Arch Linux usam o pacman e podem pedir autorização de administrador.',
    'Install update and restart': 'Instalar atualização e reiniciar',
    'Check now': 'Verificar agora',
    'Check now / install update…': 'Verificar agora / instalar atualização…',
    'Checks run in the background at startup and every six hours. The download arrow appears beside the YouTube balance when a new version is ready.': 'A verificação ocorre em segundo plano ao iniciar e a cada seis horas. A seta de download aparece ao lado do saldo do YouTube quando uma nova versão está pronta.',
    'Updates require confirmation before downloading and restarting. Your previous app files are kept in a backup folder.': 'Atualizações exigem confirmação antes de baixar e reiniciar. Os arquivos anteriores ficam em uma pasta de backup.',
    'Downloading update… {percent}%': 'Baixando atualização… {percent}%',
    'Checking for updates…': 'Verificando atualizações…',
    'Verifying and preparing update…': 'Verificando e preparando atualização…',
    'Version {version} is available · {date}': 'Versão {version} disponível · {date}',
    'Update check failed: {error}': 'Falha ao verificar atualização: {error}',
    'Your app is up to date.': 'Seu aplicativo está atualizado.',
    'The update package is not available yet. Check again later.': 'O pacote da atualização ainda não está disponível. Verifique novamente mais tarde.',
    'The release has no valid download checksum.': 'A versão não tem um código de integridade de download válido.',
    'Invalid update download size.': 'Tamanho de download da atualização inválido.',
    'Release information is too large.': 'As informações da versão são grandes demais.',
    'The update download is larger than expected.': 'O download da atualização é maior que o esperado.',
    'Update checksum verification failed. No files were changed.': 'A verificação de integridade falhou. Nenhum arquivo foi alterado.',
    'Install polkit to allow package updates from the app.': 'Instale o polkit para permitir atualizações de pacotes pelo aplicativo.',
    'The Windows update package is incomplete.': 'O pacote de atualização do Windows está incompleto.',
    'The update source is incomplete.': 'O código-fonte da atualização está incompleto.',
    'The update version does not match the release.': 'A versão da atualização não corresponde à versão publicada.',
    'The application folder could not be identified.': 'Não foi possível identificar a pasta do aplicativo.',
    'The application is still running. Close all app windows and try again.': 'O aplicativo ainda está aberto. Feche todas as janelas dele e tente novamente.',
    'The application did not close in time.': 'O aplicativo não fechou a tempo.',
    'The updated app did not finish starting.': 'O aplicativo atualizado não terminou de iniciar.',
})


# v1.3 appearance and reply threads.
PT_BR_CHAT.update({
    'Message font size (pt)': 'Tamanho da fonte das mensagens (pt)',
    'Username font size (pt)': 'Tamanho da fonte dos nomes (pt)',
    'HH = 24-hour; hh with am/pm = 12-hour. Add :ss to show seconds.': 'HH = 24 horas; hh com am/pm = 12 horas. Adicione :ss para exibir os segundos.',
    'Font sizes must be 7–48 pt; badges 10–64 px; emotes 16–128 px.': 'As fontes devem ter de 7 a 48 pt; os emblemas, de 10 a 64 px; os emotes, de 16 a 128 px.',
    'Open reply thread': 'Abrir conversa de respostas',
    'Reply thread · @{name} in {channel}': 'Conversa de respostas · @{name} em {channel}',
    'Only messages retained in this app are available.': 'Somente as mensagens mantidas neste aplicativo estão disponíveis.',
    '[Earlier message unavailable]': '[Mensagem anterior indisponível]',
})

PT_BR_CHAT.update({'Ctrl+T adds chats; choose separate tabs, side by side, stacked or a mixed feed. Middle-click empty tab-bar space to add a chat.\n\nDrag tabs to reorder; scroll over tabs to switch. Drag dividers to resize panes. Right-click a pane header for layout actions.\n\nSave a channel from Channel actions; open it later in Settings → Saved channels. YouTube checks for current broadcasts only when you click Open.\n\nYouTube and Twitch stay connected in background tabs. Pause YouTube manually from Channel actions when needed. Ctrl+F5 reconnects the current pane.': 'Ctrl+T adiciona chats; escolha abas separadas, lado a lado, empilhados ou misturados. Clique com o botão do meio no espaço vazio das abas para adicionar um chat.\n\nArraste as abas para reordenar; role sobre elas para trocar de chat. Arraste as divisórias para redimensionar os painéis. Clique com o botão direito no cabeçalho para opções de layout.\n\nSalve um canal em Ações do canal; abra-o depois em Configurações → Canais salvos. O YouTube só procura lives atuais quando você clica em Abrir.\n\nYouTube e Twitch continuam conectados nas abas em segundo plano. Pause o YouTube manualmente nas ações do canal quando precisar. Ctrl+F5 reconecta o painel atual.'})
