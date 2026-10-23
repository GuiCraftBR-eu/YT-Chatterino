# YT-Chatterino

Chat de **YouTube e Twitch** em uma interface escura inspirada no Chatterino.

## Baixar para Windows 11

**[Baixar a versão mais recente](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/latest)**

1. Na página da versão, baixe `YT-Chatterino-Windows11-1.1.0.zip` em **Assets**.
2. Clique com o botão direito no ZIP e selecione **Extrair tudo**.
3. Abra a pasta extraída e execute **YT-Chatterino.exe**.

Não é necessário instalar Python. Mantenha a pasta `_internal` ao lado do executável.
O pacote é destinado ao Windows 11 x64 (Intel/AMD).

## Recursos

- Chats do YouTube e da Twitch, com abas, painéis divididos e chats misturados.
- Leitura de chats públicos do YouTube pelo link da live, sem chave de API.
- Emotes BTTV, 7TV e Twitch, animações, badges e seletor de emotes.
- Envio de mensagens e moderação conforme as permissões da conta conectada.
- Destaques, filtros, pesquisa, sons, canais salvos e atalhos de teclado.
- Interface em português brasileiro ou inglês, com opção de manter no topo.

Para conectar contas e moderar, abra **Configurações → Tutorial** no aplicativo.
As ações de envio e moderação no YouTube exigem um cliente Google OAuth próprio
e consomem a cota do projeto. A leitura pública não consome essa cota.

## Guias

- [Manual em português](docs/README-PTBR.txt)
- [English manual](docs/README.txt)
- [Executar o código e gerar o EXE](docs/BUILDING.md)
- [Histórico de versões](CHANGELOG.md)

## Organização

```text
src/                     Código Python, ícones, logotipos e som
docs/                    Manuais e instruções de compilação
Licenses/                Avisos e licenças dos componentes distribuídos
YT-Chatterino.spec       Configuração do PyInstaller
requirements-build.txt   Dependências para gerar o pacote Windows
```

Não envie chaves, tokens, JSONs OAuth ou configurações pessoais para o repositório.
Os avisos de terceiros estão em [Licenses/NOTICE.txt](Licenses/NOTICE.txt).
Este projeto não é afiliado à Twitch, ao YouTube ou ao Chatterino.
