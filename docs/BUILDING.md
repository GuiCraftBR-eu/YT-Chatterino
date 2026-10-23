# Executar e empacotar o YT-Chatterino

## Executar pelo código-fonte

No Windows, instale o Python com Tcl/Tk e abra um terminal na raiz do repositório:

```powershell
py src/ytchatterino.pyw
```

Mantenha os cinco arquivos Python, o ícone, os logotipos e `ChatternoPing.wav`
juntos na pasta src/. A leitura dos chats do YouTube usa apenas o link da live.
Consulte [README-PTBR.txt](README-PTBR.txt) ou [README.txt](README.txt) para
conectar as contas e configurar envio de mensagens e moderação.

## Criar o executável portátil

Execute no Windows, com o Python e os arquivos do repositório:

```powershell
py -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-build.txt
.\.venv\Scripts\python.exe -m PyInstaller --noconfirm YT-Chatterino.spec
```

O resultado fica em `dist\YT-Chatterino\`. Copie `docs/README.txt`,
`docs/README-PTBR.txt` e a pasta `Licenses` para essa pasta antes de distribuí-la.
Compacte a pasta inteira: o executável precisa da pasta `_internal` ao lado.

As licenças dos componentes incluídos devem corresponder às versões usadas
no seu ambiente de compilação. A pasta `Licenses` contém os avisos do pacote
Windows distribuído com este projeto.

## Arquivos pessoais

O repositório contém o código e os recursos do aplicativo. Os JSONs OAuth,
tokens, chaves, configurações, logs e dados da conta ficam no computador de
cada usuário e não devem ser adicionados ao Git. O `.gitignore` cobre os
nomes usados pelo aplicativo.
