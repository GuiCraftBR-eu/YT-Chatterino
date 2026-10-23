# Histórico de versões

## 1.2.1

- Configurações reorganizadas: Aparência reúne Exibição, Mensagens e Destaques.
- Busca nas configurações por nomes e palavras-chave em inglês e português,
  com atalho Ctrl+F, resultados navegáveis e indicação do controle encontrado.
- Páginas criadas sob demanda, com preservação de rascunhos ao alternar páginas.
- Redimensionamento usa apenas o trecho visível durante o arrasto; restaura o
  histórico completo, a seleção e a posição ao terminar. Atualizações de mensagens
  e animações são adiadas durante o arrasto, sem desconectar os leitores.
- Atualizações de tamanho e de quebra do cabeçalho são agrupadas para reduzir
  trabalho repetido. Detalhes da medição em docs/PERFORMANCE.md.
- Testes de busca, navegação, tradução, seleção, imagens e redimensionamento.

## 1.2.0

- Respostas reais da Twitch: clique direito na mensagem → Responder à mensagem.
  O campo de envio mostra o destinatário; Esc ou × cancela a resposta.
- Exibição do contexto de respostas recebidas, inclusive no histórico recente.
- Cartões de usuário com clique esquerdo no nome, em Twitch e YouTube.
  Perfil público, avatar, dados disponíveis, destaques, menção, controles de moderação
  e até 500 mensagens desse usuário ainda retidas no canal.
- Carregamento opcional de até 200 mensagens recentes da Twitch pelo serviço usado
  pelo Chatterino; mantém ordem e evita duplicar mensagens do chat ao vivo.
- Carregamento das mensagens iniciais fornecidas pela página do YouTube.
- Limite padrão de 2.000 mensagens por painel, com ajuste global de 100 a 20.000
  e substituição por painel. Textos, referências de imagens e tags antigas são removidos.
- Edição nativa para Arch Linux: pacote pacman, PKGBUILD, lançador, ícone,
  login salvo em chaveiro criptografado e sons com players Linux.
- Testes automatizados em Windows e em um contêiner Arch Linux com Tk/Xvfb.

## 1.1.0

Primeira versão publicada neste repositório, reunindo os recursos já presentes no aplicativo:

- Leitura pública do YouTube pelo link da live, sem chave de API.
- Chats Twitch e YouTube com abas, divisão de painéis e mistura de canais.
- Envio, moderação e edição de canais conforme as permissões das plataformas.
- Emotes, badges, animações, autocompletar e seletor de emotes.
- Canais salvos, destaques, filtros, pesquisa e notificações configuráveis.
- Português brasileiro e inglês, tutorial nas configurações e manter no topo.
- Pacote portátil para Windows 11 x64, com manuais em português e inglês.

A publicação organiza o código e a documentação; não adiciona mudanças de funcionamento ao executável existente.
