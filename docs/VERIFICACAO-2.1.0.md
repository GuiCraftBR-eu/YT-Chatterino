# Verificação da versão 2.1.0

Verificação feita em 24/10/2026. A suíte QtTest passou com **96 casos/linhas de dados, zero falhas e zero ignorados** no Windows. Os perfis dos testes são isolados. Envios, moderação e edição de canais usam transportes simulados: não foram publicadas mensagens nem feitas alterações reais em canais nesta verificação.

## Mudanças verificadas ao vivo no Windows

- Janela e abas escuras sem o contorno claro/linha de base; menu dos painéis organizado em grupos.
- Leitura real da Twitch com histórico, emblemas, emotes e metadados de canal offline.
- Clique direito em uma mensagem prepara uma resposta; Esc cancela, sem envio.
- Clique do meio ativa a rolagem automática; o movimento do ponteiro rola o histórico e Esc encerra o modo.
- Contas salvas da Twitch e do YouTube carregadas. O acesso expirado do Google foi renovado sem solicitar novo login. As permissões concedidas aparecem nas configurações.
- Lista de **303 emotes disponíveis ao usuário** carregada. O botão de atualização reutilizou a autorização existente. Esse número descreve a conta usada no teste, não um catálogo fixo do aplicativo.
- Catálogos de inscritos, canal e globais separados; nomes legíveis; busca por `:)` e inserção no campo de mensagem sem envio.
- Configurações, estado das contas e tutorial conferidos em português brasileiro. O idioma e os tamanhos de fonte do usuário foram restaurados após o teste.

## Regressão dos demais recursos

| Área | Cobertura automatizada |
| --- | --- |
| Twitch e YouTube | Links, IRC, offsets Unicode, respostas, emotes nativos, badges, GIFs recebidos, mensagens pagas e continuações públicas. |
| Contas e permissões | Escopos completos, parciais e ausentes; conta desconectada, acesso expirado, autorização pendente, renovação e inicialização aguardando contas salvas. |
| Envio e moderação | Payloads Twitch/YouTube, custos, resposta removida, IDs de mensagens, exclusão, banimento, suspensão e proteção de mensagens do broadcaster. As chamadas são simuladas. |
| Edição de transmissão | Permissões de broadcaster, título/categoria na Twitch e preservação dos campos do vídeo no YouTube. Chamadas simuladas. |
| Emotes e imagens | 17 linhas de nomes legados; separação dos catálogos, paginação, atualização já autorizada, falha/repetição, composição de animações, pausa fora da tela, cache de imagens e troca real de quadros. |
| Mouse e teclado | Resposta por clique direito, Ctrl + clique direito, espaço vazio, mensagens excluídas/de sistema, rolagem automática, cancelamento, roda em 1×/2× e deltas de touchpad. |
| Histórico e layout | Limite de mensagens, deduplicação, seleção/cópia, abas vazias, mistura, divisões, zoom, retenção de raiz da conversa, sessão e geometria. |
| Aparência e pesquisa | Timestamps removidos/restaurados, datas, am/pm minúsculos, fontes separadas, busca nas configurações, idiomas, navegação e atalhos. |
| Filtros e destaques | Usuário, cargo, frase, limites de palavras, filtros e mensagens de sistema. |
| Sons e alertas | Roteamento com observador de som, silêncio inicial, transição offline→ao vivo, canais observados fechados e consultas em lote. |
| Cota e conexão | Estimativa de ações do YouTube, mudança de dia no Pacífico, abas em segundo plano, pausa manual, reconexão e rejeição de respostas antigas. |
| Atualizador | Versões, assets, SHA, caminhos, transação real no sistema de arquivos, restauração, arquivos personalizados, pastas vazias e atalhos `.lnk`. |

O [benchmark 1.3.0 → 2.1.0](BENCHMARK-1.3.0-2.1.0.md) usa o renderizador de produção com fontes e quantidade de mensagens iguais. Os scripts e os resultados brutos acompanham o repositório.

O CI compila e executa a suíte no Windows e em Arch Linux, monta e instala o pacote nativo `x86_64` e verifica a versão do executável instalado. O pacote Arch usa Qt diretamente; não depende de Wine ou de Python.

Serviços externos podem mudar independentemente do aplicativo. Disponibilidade de histórico, estrutura do YouTube, catálogos de emotes e permissões reais de cada canal continuam dependendo desses serviços. Um teste simulado aprovado não significa que uma ação destrutiva foi executada ao vivo.
