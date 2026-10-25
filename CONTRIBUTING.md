# Contribuindo

## Um MOD novo

1. Faça um fork e crie `profiles/<jogo>/` (manifesto `.sense.json` e a ponte de eventos) e `mods/<slug>/` (`mod.json` e `guide.md`), seguindo o [`docs/creating-a-mod.md`](docs/creating-a-mod.md).
2. Comece com `"status": "draft"`. O CI deste repositório compila, testa e empacota todos os MODs em cada PR.
3. Antes de marcar `"ready"`, cumpra o checklist de [`mods/README.md`](mods/README.md): testes no controle de verdade, medição de desempenho e incompatibilidades revisadas.
4. Abra o PR descrevendo o jogo, a versão testada e o que o MOD faz. Os MODs aceitos são publicados no catálogo [SenseForge-Mods](https://github.com/Pedro-A-D-S/SenseForge-Mods).

## Mudanças no framework

- C++20, no estilo do código ao redor. Rode `ctest` antes de abrir o PR, e escreva teste para lógica nova (veja `runtime-asi/tests/`).
- Nada específico de um jogo no código C++: nomes, textos e valores ficam no manifesto do MOD (seção `ui`).
- Dependências novas só pelo `tools/fetch_third_party.sh`, com versão fixada e licença compatível com MIT.

## Regras

- Não envie binários de jogos, dumps de memória ou arquivos extraídos de jogos.
- Nada que contorne DRM ou anti-cheat, nem MODs para jogos multiplayer online.
- Uma dependência só pode ir embutida no zip (`bundle: true`) se a licença dela permitir redistribuir.
