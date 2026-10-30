# Changelog

## 2.2.0

- Mostra sugestões de usuários e emotes enquanto você digita, com preenchimento por Tab.
- Adiciona um guia ilustrado, passo a passo, para conectar uma conta do YouTube.

## 2.1.4

- Mostra a prévia do emote imediatamente ao passar o mouse.
- Organiza o tutorial em assuntos navegáveis, mantendo as instruções de conexão.
- Ajusta o visual e a compactação da barra de abas e seus controles.

## 2.1.3

- Corrige a prévia de emotes ao passar o mouse.
- Mostra um selo azul com marca de verificação para usuários verificados do YouTube.
- Organiza as configurações em grupos, com navegação e visual atualizados.

## 2.1.2

- Mostra nome, prévia e provedor do emote ao passar o mouse.
- Oferece, por chat e com autorização explícita, a leitura alternativa pela API oficial quando o YouTube limita o acesso via site.
- Coordena tentativas dos chats em caso de HTTP 429, evitando solicitações repetidas durante o bloqueio.

## 2.1.1

- Explica a inclusão obrigatória de usuários de teste para apps OAuth em teste e trata recusas de login.
- Destaca campos em foco, abas sob o ponteiro e estados de botões; os botões principais mostram seus atalhos.
- A busca global aguarda a digitação, encontra a aba correta após reordenação e avisa quando a mensagem já saiu do histórico.
- Adiciona botões para limpar as buscas de mensagens e configurações.
- Ao receber HTTP 429 do YouTube, informa o bloqueio com clareza e aguarda dois minutos para tentar novamente.

## 2.1.0

- Remove a linha de base das abas e o contorno claro da janela no Windows.
- Organiza o menu dos painéis em ações do canal, pesquisa, filtros/histórico e layout.
- Corrige os nomes antigos de emotes da Twitch e separa os catálogos de inscritos, canal e globais; melhora a leitura dos nomes no seletor.
- Mostra o estado das contas e das permissões já concedidas. Atualizar emotes de inscritos reutiliza o acesso salvo.
- Clique direito prepara uma resposta na Twitch; Ctrl + clique direito mantém o menu de ações. Nada é enviado sem Enter ou Enviar.
- Adiciona rolagem automática com o botão do meio e velocidade da roda configurável, com padrão de 2×.
- Evita recriar recursos de imagens estáticas em cada repintura e aguarda as contas salvas antes de conectar a Twitch.
- Amplia a regressão de recursos e publica um benchmark reproduzível da 1.3.0 contra a 2.1.0.
- Mantém o README principal e as notas de versão em português brasileiro.

## 2.0.2

- Preserve Windows .lnk shortcut files during native updates while continuing to reject filesystem symbolic links and junctions.
- Restart the original app when a validated update is rejected before the folder swap.
- Keep the settings sidebar synchronized with shortcuts and search results; support keyboard navigation between settings pages.
- Add settings navigation and real Windows shortcut preservation regression coverage.

## 2.0.1

- Corrected an empty update-state mutation in the toolbar. An up-to-date or invalid version now keeps Install disabled.
- Added a regression check for the complete toolbar-to-update-dialog path.

## 2.0.0

- Replaced the Python/Tk application with a native C++20/Qt 6 implementation on Windows and Arch Linux.
- Ported account storage, public YouTube reading, shared Twitch IRC, sending/moderation, metadata edits, emotes/badges/GIFs, saved channels, layouts, reply threads, user cards, search/filters/highlights, sounds, appearance, translations and automatic updates.
- Virtualized message layout, bounded the document/image caches, paused offscreen animations and batched Twitch live checks.
- Added paginated emote browsing, preserved Chatterino shortcuts, native window/icon metadata, update file-preservation and rollback tests.
- Preserved compatible 1.x settings and encrypted accounts. Source installs and old Arch packages need one manual native upgrade; Windows portable updates remain supported.

Earlier Python releases remain available through the repository's 1.x tags.
