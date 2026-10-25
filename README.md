<p align="center"><img src="https://raw.githubusercontent.com/Pedro-A-D-S/SenseForge-Mods/main/assets/brand/banner.png" alt="SenseForge" width="100%"></p>

# SenseForge DevKit

Framework para criar **MODs de DualSense (PS5) para jogos de PC**: gatilhos adaptativos, lightbar, motores, vibração HD e alto-falante do controle, reagindo ao que acontece no jogo. É um add-on do [ReShade](https://reshade.me), no espírito do [RenoDX](https://github.com/clshortfuse/renodx): o framework é o mesmo para todos os jogos, e cada MOD traz só o que é do jogo dele.

Os MODs prontos para jogar ficam em [**SenseForge-Mods**](https://github.com/Pedro-A-D-S/SenseForge-Mods). Este repositório é para quem quer **criar** um.

## O que vem no kit

| Pasta | Conteúdo |
|---|---|
| `runtime-asi/` | Runtime em C++20: HID do DualSense, gatilhos adaptativos, lightbar, motores, mixer de 4 canais (alto-falante + HD haptics), máquina de estados, ponte de eventos (UDP/NDJSON), hot-reload e a aba **SenseForge** no overlay do ReShade, com presets, perfis, ajuste por efeito e as ferramentas de criador |
| `profiles/expedition33/` | MOD de referência (Clair Obscur: Expedition 33): manifesto `.sense.json` com a seção `ui` e a ponte de eventos em Lua para o UE4SS |
| `mods/` | Empacotamento: `mod.json` (arquivos, dependências por jogo, requisitos, desempenho, incompatibilidades) e o guia de cada MOD |
| `tools/` | `fetch_third_party.sh` (dependências fixadas), `mods/build_mod.sh`, `package_mod.py` e `render_docs.py` |
| `docs/creating-a-mod.md` | Passo a passo para criar um MOD |

## Começando (Windows 10/11 x64)

Requisitos: Visual Studio 2022 com C++ (MSVC), CMake 3.21+, Git for Windows (bash) e Python 3.12+.

```bash
bash tools/fetch_third_party.sh
cmake -S runtime-asi -B runtime-asi/build
cmake --build runtime-asi/build --config Release
ctest --test-dir runtime-asi/build -C Release
```

Para gerar o pacote de um MOD:

```bash
bash tools/mods/build_mod.sh expedition-33
python tools/mods/package_mod.py expedition-33 --build-dir runtime-asi/build-mod-expedition-33/Release
```

Depois leia o [`docs/creating-a-mod.md`](docs/creating-a-mod.md). Dentro do jogo, `creator_tools=1` na seção `[Dev]` do `senseforge_user.ini` liga a seção **Criador de MOD** da aba (tecla Home), com o feed de eventos, os bindings, a recarga do manifesto e o disparo de eventos.

## Publicar o seu MOD

O MOD é seu: publique onde quiser, mantendo o aviso da licença MIT. Se quiser que ele apareça no catálogo [SenseForge-Mods](https://github.com/Pedro-A-D-S/SenseForge-Mods), abra um PR aqui com a pasta `mods/<seu-jogo>/` seguindo o checklist de [`mods/README.md`](mods/README.md). Veja [`CONTRIBUTING.md`](CONTRIBUTING.md).

## Licença

[MIT](LICENSE). Componentes de terceiros estão em [`THIRD_PARTY.md`](THIRD_PARTY.md).

<sub>SenseForge não tem relação com a Sony Interactive Entertainment nem com os estúdios dos jogos. DualSense e PlayStation são marcas da Sony Interactive Entertainment.</sub>
