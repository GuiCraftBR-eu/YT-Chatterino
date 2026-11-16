## Novidades

- Vote em enquetes gratuitas da Twitch pelo aplicativo usando a extensão e uma aba conectada. Se a enquete mudar, terminar ou a aba for fechada, o aplicativo informa o resultado; votos sem confirmação explícita são mostrados como incertos.
- Aprove ou negue mensagens retidas pelo AutoMod da Twitch e altere os modos de chat disponíveis nos canais em que você tem permissão. Reconecte sua conta Twitch para conceder as novas permissões.
- O modo de moderação saiu do beta, mensagens apagadas agora ficam nas configurações de Moderação, a opacidade do overlay é ajustável e o botão de fechar abas foi corrigido.
- No Windows, o plugin do OBS verifica atualizações e as aplica no próximo reinício do OBS. Instale a versão 2.5.6 manualmente uma vez para ativar esse recurso.
- No Linux, o aplicativo e o plugin do OBS agora são distribuídos como Flatpak. O instalador opcional configura atualizações automáticas para seu usuário. O plugin requer o OBS Studio Flatpak 33 ou superior.

Para votar, mantenha a Twitch aberta no navegador com a extensão 2.5.6 conectada. A interface da Twitch pode mudar. Apostas com pontos, enquetes do YouTube e da Kick continuam no navegador; o aplicativo não envia apostas nem gasta pontos automaticamente. A disponibilidade do AutoMod e dos modos de chat depende das permissões da conta e da plataforma.

O fluxo de votação foi testado em um navegador com uma página simulada baseada na interface da Twitch. Uma votação real autenticada ainda não foi confirmada.

No Linux, o instalador pode copiar suas configurações nativas antigas sem apagar os arquivos originais, mas é necessário conectar as contas novamente no ambiente Flatpak. O instalador usa Flatpak, Python 3 e o timer do systemd de usuário; os próprios aplicativos são nativos em C++/Qt.
