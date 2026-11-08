## YT-Chatterino 2.4.7

- O chat do YouTube sem chave consulta mais rapidamente quando o servidor recomenda dez segundos e distribui as mensagens em lotes de forma mais fluida. Após limitação de requisições, o aplicativo retoma o intervalo recomendado pelo YouTube.
- O cartão de usuário mostra quando a pessoa começou a seguir na Twitch, quando a conta conectada tem a permissão e o acesso necessários. Também consulta inscrições públicas do YouTube quando possível.
- Um aviso discreto aparece quando a rolagem é pausada ao passar o mouse sobre o chat.
- Pequenos ajustes visuais e testes para Windows e Arch Linux.

**Twitch:** consultar outros usuários exige ser dono ou moderador do canal e autorizar a leitura de seguidores. A data da própria conta usa uma permissão separada. **YouTube:** a consulta exige uma conta conectada e depende da privacidade das inscrições. A API pública da Kick não informa essas datas.

**Windows:** extraia o ZIP antes de abrir. **Arch Linux:** instale com `sudo pacman -U ./yt-chatterino-2.4.7-1-x86_64.pkg.tar.zst`.
