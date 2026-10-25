# Benchmark: 1.3.0 → 2.1.0

Medições em **24/10/2026**, no mesmo computador: Windows 11 Pro x64, build 26200; Ryzen 7 5700X (8 núcleos/16 threads), 15,9 GiB de RAM. São **sete execuções medidas por versão**, em processos separados, depois de um aquecimento por versão. A ordem foi alternada para reduzir o efeito da ordem de execução.

## Resultados

Valores: **mediana (mínimo–máximo)**. Menor é melhor. A última coluna compara as medianas.

| Tarefa | 1.3.0 | 2.1.0 | Redução do tempo |
| --- | --- | --- | --- |
| Inicialização até o primeiro quadro | 528,8 ms (508,5–593,2) | 297,1 ms (289,0–320,4) | 43,8% |
| Carregar 2.000 mensagens | 871,7 ms (850,3–876,5) | 191,4 ms (187,8–205,0) | 78,0% |
| 80 redimensionamentos | 4968,1 ms (4865,7–5217,8) | 448,8 ms (434,4–536,5) | 91,0% |

Neste teste, o carregamento do histórico levou aproximadamente **4,6 vezes menos tempo**, e os redimensionamentos, **11,1 vezes menos tempo**. Esses fatores descrevem somente este cenário, não todas as interações ou computadores.

[Dados brutos das 14 execuções e resumo JSON](benchmarks/1.3.0-2.1.0-windows.json).

## O que foi comparado

- **1.3.0:** fontes intactos da [tag v1.3.0](https://github.com/GuiCraftBR-eu/YT-Chatterino/tree/v1.3.0), executados com **Python 3.14.7 / Tk 9.0.4**. Os 15 arquivos de código foram conferidos com a tag; diferenças locais de fim de linha CRLF/LF não alteram o conteúdo do código.
- **2.1.0:** compilação Release, **C++20, GCC 13.1 e Qt 6.8.3**. O alvo de benchmark usa as janelas, estilos, carregamento de histórico e renderizador de produção.
- Ambos usam perfil isolado, janela visível em **520×760**, escala de 100%, fontes Segoe UI de **12 pt** para mensagem e nome (altura medida: **21 px** nos dois), timestamps e fundos alternados.
- **2.000 mensagens idênticas**, com 20 nomes, timestamp, link e texto longo que quebra em uma janela estreita. Sem emotes/imagens para impedir que downloads ou decodificadores diferentes distorçam esta comparação.
- Histórico carregado pelas rotas normais: `load_recent` e bomba de eventos na 1.3.0; `Pane::reload` na 2.1.0. O temporizador inclui a estabilização do layout.
- **80 mudanças** por execução, repetindo as dimensões `360×680`, `900×760`, `520×760` e `680×640`, processando os eventos e aguardando a estabilização final. A mudança de largura usa os atrasos de produção de cada versão.
- A inicialização é medida externamente, da criação do processo ao sinal de primeiro quadro pronto. Todos os processos terminaram normalmente, com 2.000 mensagens e **zero requisições de rede**.
- A 2.1.0 manteve **11 documentos de mensagens** no cache neste cenário; os dados das 2.000 mensagens continuaram retidos. O limite geral do cache é 128 documentos.

## Limites da comparação

Este teste mede o aplicativo local, sem contas, serviços externos, alertas ou atualizações em segundo plano. **Não é um teste de inicialização a frio do computador.** Usa cache do sistema após aquecimento e inclui a variação normal do Windows.

A 1.3.0 foi executada pelo seu código-fonte, **não pelo executável antigo do PyInstaller**. O resultado de inicialização inclui o interpretador/importações normais, mas não a extração ou o empacotamento do executável distribuído. Não se deve usar esta tabela para prometer o tempo exato de abertura daquele ZIP antigo.

A rede ficou bloqueada nos dois testes; portanto, esta tabela não mede o tempo até receber o primeiro chat real do YouTube/Twitch. Também não compara o consumo de RAM/CPU nem a reprodução de GIFs. O pacote Arch não foi submetido a este benchmark no desktop Windows.

## Reproduzir no Windows

Em uma pasta de trabalho separada, obtenha a tag antiga sem modificar seu checkout atual:

```powershell
git worktree add --detach ../yt-chatterino-130 v1.3.0
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DBUILD_BENCHMARKS=ON
cmake --build build --parallel 2
python benchmarks/compare.py --legacy-source ../yt-chatterino-130/src --native build/chatterino_benchmark.exe --output benchmark-resultados --runs 7
```

Use Python com Tk, Qt/MinGW disponíveis no PATH e o mesmo nível de escala/fontes. Deixe o aplicativo normal fechado, evite outras cargas de trabalho e use uma pasta de resultados nova. Os perfis criados pelo script são isolados e não devem ser enviados junto dos dados públicos. A compilação normal não inclui o alvo de benchmark; Python só é necessário para comparar a versão antiga.

## O que mudou, em duas linhas

A 1.3.0 usava Python/Tk; a 2.1.0 usa C++/Qt, desenha apenas as mensagens visíveis e mantém caches limitados.  
A 2.1.0 também melhora os nomes e o seletor de emotes, indica as permissões das contas e adiciona resposta direta e rolagem automática.
