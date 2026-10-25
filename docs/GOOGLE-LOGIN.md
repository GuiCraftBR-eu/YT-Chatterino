# Login do Google bloqueado: 403 / access_denied

Se o Google disser que o app está em teste e só pode ser acessado por testadores aprovados, a conta escolhida ainda não está autorizada no projeto OAuth usado pelo aplicativo. Esse bloqueio é definido pelo Google; reinstalar o YT-Chatterino não altera a lista de testadores.

## Liberar uma conta de teste

O responsável pelo projeto deve:

1. Abrir o [Google Cloud Console](https://console.cloud.google.com/) e selecionar **o projeto que criou o JSON OAuth escolhido no aplicativo**.
2. Acessar **Google Auth Platform → Público-alvo (Audience)**. Em interfaces antigas, procurar **APIs e serviços → Tela de consentimento OAuth**.
3. Para testes com contas pessoais externas à organização, usar o público **Externo (External)**. Em **Usuários de teste (Test users)**, clicar em **Adicionar usuários** e incluir o endereço exato da conta Google que a pessoa usará no navegador.
4. Salvar. No YT-Chatterino, abrir **Configurações → Contas**, cancelar o login pendente e tentar entrar novamente, escolhendo a conta adicionada.

Adicionar alguém como colaborador do repositório ou editor do Google Cloud não substitui a inclusão em **Usuários de teste**. Escolher outro projeto no console também não modifica o projeto do JSON já selecionado no aplicativo.

## Usar um projeto próprio

Se você não controla o projeto do JSON atual, pode pedir ao responsável que adicione sua conta para testes ou configurar seu próprio projeto:

1. Criar um projeto no Google Cloud e ativar **YouTube Data API v3**.
2. Configurar o Google Auth Platform e adicionar a própria conta como usuário de teste.
3. Criar um cliente OAuth do tipo **Aplicativo para computador (Desktop app)** e baixar seu JSON.
4. Selecionar esse arquivo em **Configurações → Contas → Escolher JSON OAuth do Google** e entrar novamente.

Não compartilhe tokens, perfis salvos ou arquivos privados de credenciais. Não é necessário mudar o código do aplicativo ou enviar seu JSON para este repositório.

## Distribuição pública e outros bloqueios

A lista de testadores serve para testes. Para oferecer um mesmo cliente OAuth ao público, o responsável deve seguir as exigências de publicação e verificação do Google para os escopos solicitados. Trocar o status para produção não garante aprovação nem elimina as exigências de verificação.

`access_denied` também pode ocorrer ao recusar permissões ou por políticas de uma organização. Se a mensagem do navegador for diferente da descrita acima, confira os detalhes e, quando necessário, fale com o administrador da conta. O aplicativo não pode contornar essas restrições.

Projetos externos em teste podem exigir novo login após sete dias. **A leitura pública dos chats do YouTube continua disponível sem login**; envio e moderação exigem autenticação e permissões no canal.
