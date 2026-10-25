# SenseForge MODs

Cada pasta aqui é um MOD publicável: `mods/<slug>/`. O slug é o nome do jogo em minúsculas, com hífens, por exemplo `expedition-33` ou `black-myth-wukong`. Como criar um MOD do zero está em [`docs/creating-a-mod.md`](../docs/creating-a-mod.md).

```
mods/<slug>/
├─ mod.json     identidade, arquivos do pacote, dependências, desempenho, incompatibilidades
├─ guide.md     parte do guia escrita à mão (instalação, primeiro uso, problemas, desinstalação)
└─ files/       arquivos que só existem no pacote (por exemplo o mods.txt do UE4SS)
```

## Do desenvolvimento à publicação

1. **`status: "draft"`**: o `ci.yml` compila, testa e empacota o MOD em todo PR. Ele aparece nos artifacts do Actions e nunca é publicado.
2. **`status: "ready"`**: o MOD passou no checklist abaixo.
3. **Publicar**: o MOD é seu e pode ser publicado onde você quiser, com o `package_mod.py --require-ready`. Para ele entrar no catálogo oficial [`SenseForge-Mods`](https://github.com/Pedro-A-D-S/SenseForge-Mods), abra um PR neste repositório. Veja o [`CONTRIBUTING.md`](../CONTRIBUTING.md).

## Checklist de MOD pronto

- [ ] O `sf_manifest_check` passa limpo no manifesto (`.sense.json`).
- [ ] O manifesto tem a seção `ui`: nomes, descrições, entradas internas marcadas como `hidden` e `guide_url` apontando para `mods/<slug>.md` no repo público.
- [ ] Houve uma sessão física com debrief (`/senseforge-gameplay-debrief`), e cada efeito foi sentido no controle.
- [ ] O desempenho foi medido em A/B (vanilla × MOD) e os números estão em `performance.results`. O pacote não faz dumps de desenvolvimento no boot.
- [ ] O `known_issues` foi revisado: outras ferramentas de DualSense, Steam Input, outros mods e overlays.
- [ ] O `guide.md` foi revisado, e uma instalação feita só a partir do zip e do guia funcionou numa pasta limpa.
- [ ] Toda dependência com `bundle: true` tem `url` e `sha256` e uma licença que permite redistribuir. As que não podem ir no zip ficam `bundle: false`, com `install_steps`.
