# Native experimental de combate

`WorldState::CaptureSrpOriginalImpactFrame` considera os atores já carregados
no mesmo worldspace. O produtor extrai um limite externo dos controladores
físicos e o native verifica esse limite na linha do golpe. Interseção,
geometria ausente, ragdoll e transformação pendente recusam o frame. O hash
do frame inclui a identidade, conexão, pose e limites de cada ator observado.
Mover ou substituir um deles invalida a operação antes da aplicação.
Uma gravação de XP ou da outbox de outro atacante preserva o mesmo frame
quando a identidade e a geometria do observador continuam iguais.

`SrpOriginalImpact::Reconcile` conclui a criação do cadáver tanto no replay
de `Apply` quanto em `Cancel`. Um pedido letal já gravado precisa chegar a
`DEAD` e ser salvo antes do ACK. Divergência entre o identificador do pedido
de morte e o recibo do golpe interrompe a recuperação para revisão.

`ActionListener::OnSpellCast` valida o host antes de consultar o conjurador.
Uma entrada antiga de hosting também exige o ator carregado. Referências
originais controladas e atores em transição são recusados nesse caminho.

## Compatibilidade

O limite externo é opcional no leitor da cena. Cenas antigas permanecem
legíveis e exigem nova produção para certificar obstruções por outros atores.
A procedência dos NIFs, as escalas e os transforms continuam vinculados aos
hashes da cena. O campo adicional do frame participa do recibo existente.
Recibos de aplicação confirmada mantêm precedência durante a recuperação.
Uma intenção antiga sem o novo frame pode ser cancelada conservadoramente.

O rollback usa os recibos e o world atuais. A restauração de snapshots sobre
progresso posterior exige reconciliação própria e permanece fora da candidata.

## Evidências

Os 33 casos C++ passaram, com 311 verificações. Os 12 testes do produtor
Python passaram. O probe leu os 39 NIFs fixados pelo manifesto existente,
com 247 combinações de ator e raça, e confirmou seus limites externos.
Os 730 arquivos de base conferidos coincidem com o HEAD anterior do PR #7.

A primeira execução teve 32 casos aprovados e um reprovado. A fixture com
dois atacantes ainda usava uma cena sem limites externos para o observador.
Ela passou a fornecer a geometria correspondente. O hash também passou a
usar identidade e conexão, preservando o frame durante a gravação de outra
outbox. As expectativas de dano e deduplicação do teste foram preservadas.

Os arquivos em `evidence/native-combat-20261111/` registram o resultado dos
testes executados, os hashes dos fontes e a paridade da base de compilação.
O probe dos controladores pode ser reproduzido com os assets locais já
fixados pelo manifesto.

```sh
python -B -m unittest discover -s tools/srp-collision-scene -p 'test_actor*.py'
python tools/srp-collision-scene/probe_controller_bounds.py \
  --manifest actor-manifest.json --assets assets --out bounds-result.json
```

## Gates de gameplay

`SrpDynamicSnapshotComplete` mantém a recusa quando
`physicalBaselineVerified` está ativo e `untrackedPhysics` contém referências.
Shimmermist conserva 18 corpos móveis nessa condição. O limite externo do
controlador de um ator cobre apenas esse controlador. Corpos móveis,
ragdolls e poses físicas especiais ainda exigem produtores próprios.

Ataques NPC→jogador, XP ligado à morte, reset original CELL/EZ e autoridade
completa de magias permanecem bloqueados. A ponte sobrenatural ainda aguarda
transformação, combate e retorno observados nas duas sessões do Skyrim.
O pacote de reserva sobrenatural conserva seu native anterior validado,
identificado pelos hashes do pacote. Este patch possui gates próprios.

**RELEASE_BLOCKED**. A expansão tem zero referências com ciclo multiplayer
completo homologado.
