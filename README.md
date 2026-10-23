# YT-Chatterino

Chat de **YouTube e Twitch** em uma interface escura inspirada no Chatterino.

## Baixar para Windows 11

**[Baixar a versão mais recente](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/latest)**

1. Na página da versão, baixe `YT-Chatterino-Windows11-1.2.2.zip` em **Assets**.
2. Clique com o botão direito no ZIP e selecione **Extrair tudo**.
3. Abra a pasta extraída e execute **YT-Chatterino.exe**.

Não é necessário instalar Python. Mantenha a pasta `_internal` ao lado do executável.
O pacote é destinado ao Windows 11 x64 (Intel/AMD).

## Arch Linux nativo

Baixe o pacote `yt-chatterino-1.2.2-1-any.pkg.tar.zst` na mesma página e instale
com `sudo pacman -U ./yt-chatterino-1.2.2-1-any.pkg.tar.zst`.
O menu de aplicativos terá **YT-Chatterino**. Não usa Wine.
Também há um `.tar.gz` com o código e o PKGBUILD para compilar/instalar com `makepkg -si`.
Veja o [guia do Arch Linux](docs/ARCH-LINUX.md) para dependências, chaveiro e sons.

## Atualizações pelo aplicativo

A partir da 1.2.2, a seta **⬇** aparece quando há uma versão nova. Clique nela e
confirme **Instalar atualização e reiniciar**. Também há **Configurações → Atualizações**.
No Arch instalado por pacman, o polkit pede autorização para atualizar o pacote.
Quem usa 1.2.1 ou anterior precisa instalar a 1.2.2 manualmente uma vez.

## Recursos

- Chats do YouTube e da Twitch, com abas, painéis divididos e chats misturados.
- Leitura de chats públicos do YouTube pelo link da live, sem chave de API.
- Emotes BTTV, 7TV e Twitch, animações, badges e seletor de emotes.
- Envio de mensagens e moderação conforme as permissões da conta conectada.
- Configurações com busca em inglês e português; Exibição, Mensagens e Destaques em Aparência.
- Destaques, filtros, pesquisa, sons, canais salvos e atalhos de teclado.
- Interface em português brasileiro ou inglês, com opção de manter no topo.
- Datas em DD/MM/AAAA por padrão e formatos de horário de 12/24 horas, com segundos opcionais.
- Saldo do YouTube exibido apenas quando o painel focado inclui um chat do YouTube.
- Respostas reais da Twitch pelo menu de clique direito nas mensagens.
- Cartão de usuário ao clicar no nome, com perfil público e mensagens retidas.
- Histórico recente ao abrir chats, quando disponível.
- Limite padrão de 2.000 mensagens por painel, configurável em **Configurações → Aparência → Mensagens**.
  O menu do cabeçalho permite um limite específico para cada painel.

Para conectar contas e moderar, abra **Configurações → Tutorial** no aplicativo.
As ações de envio e moderação no YouTube exigem um cliente Google OAuth próprio
e consomem a cota do projeto. A leitura pública não consome essa cota.

## Guias

- [Manual em português](docs/README-PTBR.txt)
- [English manual](docs/README.txt)
- [Executar o código e gerar o EXE](docs/BUILDING.md)
- [Instalar no Arch Linux](docs/ARCH-LINUX.md)
- [Histórico de versões](CHANGELOG.md)
- [Medição de desempenho](docs/PERFORMANCE.md)

## Organização

```text
src/                     Código Python, ícones, logotipos e som
docs/                    Manuais e instruções de compilação
Licenses/                Avisos e licenças dos componentes distribuídos
YT-Chatterino.spec       Configuração do PyInstaller
requirements-build.txt   Dependências para gerar o pacote Windows
packaging/arch/          Pacote nativo, lançador e integração ao menu do Arch
tests/                   Testes de lógica e da interface Tk
```

Não envie chaves, tokens, JSONs OAuth ou configurações pessoais para o repositório.
Os avisos de terceiros estão em [Licenses/NOTICE.txt](Licenses/NOTICE.txt).
Este projeto não é afiliado à Twitch, ao YouTube ou ao Chatterino.

### Histórico e dados de usuários

O histórico da Twitch usa o serviço público independente
[recent-messages.robotty.de](https://recent-messages.robotty.de/), também usado pelo Chatterino.
Ele recebe o nome do canal, sem tokens de conta. Pode ser desativado em **Aparência → Mensagens**.
Não é um arquivo completo: canais podem não ter mensagens anteriores disponíveis.
O YouTube fornece uma janela recente de mensagens pelo próprio chat público.

Cartões mostram os dados fornecidos pelas plataformas e as mensagens mantidas
nesta sessão. A Twitch informa os meses de inscrição nas tags das mensagens,
mas não expõe publicamente todos os detalhes de inscrição de outros usuários.
Os detalhes adicionais de canais do YouTube usam o login Google já configurado
e custam 1 unidade de API por perfil consultado (com cache). Sem login, ainda é
possível ver o nome, ID, emblemas e mensagens retidas.
