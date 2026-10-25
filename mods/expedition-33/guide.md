## Instalação

1. **Instale o ReShade com suporte a add-ons**, seguindo os passos da seção Dependências acima.
2. **Baixe o zip** deste MOD na tabela de downloads.
3. **Abra a pasta do jogo.** Na Steam, clique com o botão direito em *Clair Obscur: Expedition 33* → *Gerenciar* → *Ver arquivos locais*. Depois entre em `Sandfall/Binaries/Win64`, onde fica o `SandFall-Win64-Shipping.exe`.
4. **Extraia o conteúdo do zip** nessa pasta e aceite substituir os arquivos. A pasta fica assim:

   ```
   Win64/
   ├─ SandFall-Win64-Shipping.exe          (do jogo)
   ├─ dxgi.dll, reshade.ini                (do ReShade)
   ├─ dwmapi.dll                           (UE4SS)
   ├─ senseforge.expedition-33.addon64     (SenseForge)
   ├─ expedition33.sense.json              (perfil do jogo)
   ├─ senseforge_user.ini                  (seus ajustes)
   └─ ue4ss/
      ├─ UE4SS.dll, UE4SS-settings.ini
      └─ Mods/
         ├─ mods.txt
         └─ SenseForgeBridge/Scripts/main.lua
   ```

5. **Conecte o DualSense** antes de abrir o jogo. O cabo USB é recomendado.

## Primeiro uso

- Abra o jogo normalmente pela Steam. Nenhuma janela extra aparece.
- **Espere uns 15 segundos** depois do menu: o SenseForge liga depois desse tempo, de propósito.
- Aperte **Home** para abrir o overlay do ReShade e vá na aba **SenseForge**:
  - **Preset**: *Desligado* deixa o controle como no jogo original. *Preset 1, 2 e 3* guardam ajustes separados (por exemplo, um para jogar à noite com tudo mais leve).
  - **Perfil**: *Fraco*, *Médio* ou *Forte* preenchem as intensidades de uma vez. Se você mexer à mão, o perfil vira *Personalizado*.
  - **Status ao vivo**: mostra se o controle foi encontrado e o que o SenseForge está fazendo agora.
  - **Ajuste por efeito**: liga ou desliga cada efeito, muda a força de cada gatilho e tem um botão **Testar** para sentir o efeito na hora.
- Os ajustes são salvos sozinhos no `senseforge_user.ini`.

## Solução de problemas

| Sintoma | O que fazer |
|---|---|
| A aba SenseForge não aparece | Confira se o ReShade instalado é o **with full add-on support** e se o arquivo `senseforge.expedition-33.addon64` está na pasta `Win64`. |
| "Controle: não encontrado" | Conecte o DualSense antes de abrir o jogo e feche DSX, DS4Windows e programas parecidos. |
| Duas abas SenseForge | Apague o `senseforge.addon64` antigo. Fica só o `senseforge.expedition-33.addon64`. |
| Sem vibração HD nem som no controle | Isso só funciona por cabo USB. Em *Configurações de som do Windows*, o dispositivo "Wireless Controller" precisa estar ativo. |
| O efeito parece fraco | Em **Perfil**, escolha *Forte*. Para ir além do padrão, use *Gatilho L2/R2 → Máximo*. |

Se precisar de ajuda, abra uma *issue* neste repositório. Anexe o log de `%LOCALAPPDATA%\senseforge\logs\` (o arquivo mais recente) e o `ue4ss\UE4SS.log` da pasta do jogo.

## Desinstalação

Apague da pasta `Win64` os arquivos `senseforge.expedition-33.addon64`, `expedition33.sense.json`, `senseforge_user.ini` e a pasta `ue4ss/Mods/SenseForgeBridge`. Para remover também o UE4SS, apague o `dwmapi.dll` e a pasta `ue4ss`. O ReShade tem desinstalador próprio.
