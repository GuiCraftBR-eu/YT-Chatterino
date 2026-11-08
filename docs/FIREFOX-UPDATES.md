# Atualizações da extensão do Firefox

A extensão não listada inclui uma URL HTTPS fixa para `browser-extension/updates.json`. Depois de instalar uma versão assinada com essa URL, o Firefox consulta esse arquivo e instala versões superiores assinadas pela Mozilla. As instalações temporárias e as versões antigas sem `update_url` precisam ser substituídas uma vez por uma versão assinada atual.

Para cada versão futura:

1. Incremente `version` em `browser-extension/manifest.json` e envie a extensão à Mozilla para assinatura não listada, com a mesma ID. Não compartilhe credenciais.
2. Baixe o `.xpi` assinado. Execute `python packaging/firefox_update.py caminho/do/arquivo.xpi`; o script valida ID, versão, presença da assinatura e URL e prepara `updates.json` com SHA-256.
3. Publique o mesmo `.xpi` na release `v<versão>` com o nome `YT-Chatterino-Firefox-<versão>.xpi`. Só depois publique o `updates.json` atualizado no `main`. Assim o Firefox não receberá um link para um arquivo ainda inexistente.

A presença de arquivos de assinatura no XPI não substitui a verificação criptográfica feita pelo Firefox. Não aponte o manifesto para um pacote não assinado ou para outro arquivo.
