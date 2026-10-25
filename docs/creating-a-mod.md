# Como criar um SenseForge MOD

Um MOD do SenseForge liga o que acontece num jogo ao DualSense: gatilhos adaptativos, lightbar, motores, HD haptics e alto-falante. O **framework** é o mesmo para todos os jogos:
- o runtime `senseforge.<slug>.addon64`, carregado pelo ReShade;
- a aba **SenseForge** no overlay do ReShade;
- o pipeline de publicação.

Cada **MOD** traz só o que é do jogo dele. O Clair Obscur: Expedition 33 (`mods/expedition-33/`) é o exemplo de referência.

## 1. As três peças de um MOD

| Peça | Arquivo | O que define |
|---|---|---|
| **Perfil do jogo** | `profiles/<jogo>/<jogo>.sense.json` | estados contínuos (`states`), eventos instantâneos (`event_effects`), bindings de memória e a seção `ui` (como a aba mostra o MOD) |
| **Fonte de eventos** | no E33, `profiles/expedition33/lua/senseforge_bridge.lua` | como o jogo avisa o SenseForge. No E33 é um mod Lua do UE4SS que grava eventos em NDJSON. Outro jogo pode usar REFramework, leitura de memória ou outra ponte |
| **Pacote** | `mods/<slug>/mod.json` + `guide.md` | arquivos do zip, dependências (dinâmicas por jogo), requisitos, desempenho, incompatibilidades e guia |

### Eventos que o runtime entende

A fonte de eventos manda uma linha JSON por evento, por UDP em `127.0.0.1:6970` ou acrescentando ao arquivo `senseforge_events.ndjson` na pasta do executável:

```json
{"type":"sf_flash","id":"parry","scale":1.0}
{"type":"sf_state","id":"e33_free_aim","value":1.0}
```

- **`sf_flash`**: um efeito curto (lightbar, motores, pulso nos gatilhos), definido em `event_effects` pelo mesmo `id`.
- **`sf_state`**: um valor persistente que as `condition` dos `states` usam, por exemplo `"e33_free_aim == 1"`. O estado de maior `priority` que casar define os gatilhos. Se nenhum casar, os gatilhos ficam soltos.

## 2. A seção `ui` (como o MOD aparece na aba)

O framework não conhece nenhum jogo: nomes, explicações e ordem vêm do próprio MOD.

```json
"ui": {
  "guide_url": "https://github.com/Pedro-A-D-S/SenseForge-Mods/blob/main/mods/<slug>.md",
  "credits": "MOD <jogo> por <você> · SenseForge",
  "states": {
    "combat":   {"name": "Combate (R2)", "description": "R2 travado nas batalhas.", "order": 1},
    "explore":  {"name": "Exploração", "hidden": true}
  },
  "events": {
    "parry": {"name": "Parry", "description": "Lightbar dourada e vibração.", "order": 1},
    "dodge": "Esquiva"
  },
  "categories": {"combat": {"name": "Combate"}},
  "profiles": {"weak": {"trigger_preset": 1, "haptic_preset": 1, "event_intensity": 50}}
}
```

- `name` e `description`: o nome e a dica (tooltip) na aba. A forma curta `"id": "Nome"` também vale.
- `hidden`: estados ou eventos internos, que não aparecem para o usuário.
- `order`: ordem na tabela. Empates mantêm a ordem do manifesto.
- `profiles` (`weak`, `medium`, `strong`): valores base dos perfis Fraco, Médio e Forte deste MOD. Campos omitidos usam o padrão do framework.
- O `sf_manifest_check` acusa entradas de `ui` que apontam para ids inexistentes.

## 3. Ferramentas de criador (dentro do jogo)

Coloque `creator_tools=1` na seção `[Dev]` do `senseforge_user.ini` do jogo. A aba SenseForge passa a mostrar a seção **Criador de MOD**, com:

- o **feed ao vivo** dos últimos 50 eventos, indicando se cada um foi aplicado ou ignorado e por quê (por exemplo, "sem event_effect no manifesto" ou "evento desligado no painel");
- o **estado vencedor** e a prioridade dele, mais o valor de cada binding;
- **Recarregar manifesto**, que também mostra os avisos de validação;
- **Disparar evento** com qualquer id, inclusive um que ainda não está no manifesto;
- **Copiar status**, para colar num relatório ou num debrief.

Com `ue4ss_dumps=1` na seção `[Dev]`, o mod Lua do E33 gera no boot o `UE4SS_ObjectDump.txt` e os headers UHT, que são o mapa para achar funções e propriedades do jogo. É só para desenvolvimento: no pacote público isso fica desligado.

## 4. Do perfil ao pacote

1. Crie `mods/<slug>/mod.json`, partindo do `mods/expedition-33/mod.json`:
   - `build.cmake_game_slug` nomeia o addon: `senseforge.<slug>.addon64`;
   - `files[]` lista o que vai no zip e para onde. Use `build:<arquivo>` para saídas do CMake, e `strip_ini_sections: ["Dev"]` para tirar chaves de desenvolvimento;
   - `dependencies[]` lista o que o jogo precisa. Com `bundle: true`, a dependência é baixada, conferida pelo `sha256` e colocada no zip (só se a licença permitir). Com `bundle: false`, ela vira passo a passo no guia (`install_steps`).
2. Escreva o `mods/<slug>/guide.md`: instalação, primeiro uso, problemas e desinstalação. O resto do guia é gerado a partir do `mod.json`.
3. Teste localmente:

   ```bash
   bash tools/mods/build_mod.sh <slug>
   python tools/mods/package_mod.py <slug> --build-dir runtime-asi/build-mod-<slug>/Release
   python tools/mods/render_docs.py --out /tmp/preview
   ```

4. Siga o checklist em [`mods/README.md`](../mods/README.md) e publique com a tag `mod/<slug>/v<versão>`.

## 5. Regras que o pacote e a publicação conferem

O `package_mod.py`, o `render_docs.py` e o `tools/security/preflight.py` (rodado no CI de todo PR) recusam um `mod.json` que sair destas regras. Elas existem porque o zip é baixado por outras pessoas e passa pelo canal oficial de MODs.

| Campo | Regra | Mensagem quando falha |
|---|---|---|
| `slug` | igual ao nome da pasta e no formato `^[a-z0-9][a-z0-9-]{0,63}$` (minúsculas, números e hífen) | `invalid slug` ou `slug ... != folder` |
| `files[].from` | caminho relativo dentro do repositório (nada de `..`, caminho absoluto, `\`, `:` ou `.git`), ou `build:<arquivo>` sem pastas | `files[].from ... leaves the repo` |
| `files[].to` | caminho relativo dentro do zip, nunca vazio | `files[].to ... leaves the package` |
| `guide_body` | um arquivo dentro de `mods/<slug>/` | `guide_body must be a file in mods/<slug>/` |
| `dependencies[].id` | nome simples: `^[a-z0-9][a-z0-9_-]{0,63}$` (vira nome de pasta no cache) | `dependency id ... must be a plain name` |
| `dependencies[].url` (`bundle: true`) | `https://` e com `sha256` | `must use an https url` |

Além do `mod.json`, o preflight confere o que vai no zip:

- **`senseforge_user.ini`**: com `strip_ini_sections: ["Dev"]`, o pacote não leva `agent_autostart` nem `creator_tools` ligados. O cabeçalho `[Dev] ; comentário` também é removido, do mesmo jeito que o runtime lê o arquivo.
- **`mods.txt` do UE4SS**: `CheatManagerEnablerMod`, `ConsoleCommandsMod` e `ConsoleEnablerMod` ficam em `: 0`. O guia promete o console desligado.
- **Manifesto**: ids de estados, categorias e eventos só com letras, números, `_`, `.` e `-` (até 64 caracteres), e `ui.guide_url` com `https://`.
- **Segredos**: nenhum arquivo versionado pode conter token, chave privada ou credencial.
- **Workflows**: ações fixadas por commit SHA e nenhum `${{ inputs.* }}` colado dentro de um script.

Para rodar o mesmo gate antes de abrir o PR:

```bash
python -I tools/security/preflight.py
python -I tools/mods/test_package_mod.py
```
