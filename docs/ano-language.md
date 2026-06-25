# ano (あの)

A staging language for entity-component systems. FP / LISP / APL lineage, ASCII surface, intended as a Lua-class embeddable scripting layer.

A script states predicates over registered components. The host engine resolves which entities satisfy them. The selection predicate is the entity reference. The name あの is the distal demonstrative ("that one over there").

Architecture. Scripts stage calls to host-registered functions, compile to bytecode, JIT the predicate-and-emit hot path, and return an effect buffer describing the work. The host interprets the buffer. The execution model is eBPF-shaped: the script invokes only registered functions. Component types and read/write footprints reside in the registry, declared once at registration and referenced by name in scripts.

Table 1 places one canonical task across every language raised in design discussion. Table 2 enumerates candidate ano surfaces. Tokens are representative rather than exact. Several languages express the task by returning a new world rather than mutating, noted per row.

---

## Table 1. The canonical task across mentioned languages

Canonical task: +1000 gold to every entity that is Nord and has Two-Handed > 60.
Secondary tasks where defined: Fold (sum gold across all Nords, returns a scalar) and Despawn (remove all Dead entities, structural effect).

### Skyrim console (Canonical)
```text
player.additem f 1000
```
Operates on the player alias. Set-wide selection is not expressible.

### Papyrus (Canonical)
```papyrus
int i = 0
while i < n
  if a[i].GetRace() == Nord && a[i].GetAV("TwoHanded") > 60
    a[i].Gold += 1000
  endif
  i += 1
endwhile
```

### APL (Canonical)
```apl
gold +← 1000 × nord ∧ 60 < twoH
```

### APL (Fold)
```apl
+/ gold × nord
```

### APL (Despawn)
```apl
entities ↓⍨← dead
```

### BQN (Canonical)
```bqn
gold +↩ 1000 × nord ∧ 60 < twoH
```

### BQN (Fold)
```bqn
+´ gold × nord
```

### BQN (Despawn)
```bqn
(¬dead) / entities
```

### J (Canonical)
```j
gold =: gold + 1000 * nord *. 60 < twoH
```

### J (Fold)
```j
+/ gold * nord
```

### J (Despawn)
```j
entities #~ -. dead
```

### q / kdb+ (Canonical)
```q
update gold:gold+1000 from p where race=`nord, twohanded>60
```

### q / kdb+ (Fold)
```q
select sum gold from p where race=`nord
```

### q / kdb+ (Despawn)
```q
delete from p where dead
```

### k (Canonical)
```k
p:@[p;&(race=`nord)&th>60;{x+1000}@gold]
```

### Haskell, comprehension (Canonical)
```haskell
[ e{gold = gold e + 1000} | e <- world, race e == Nord, twoHanded e > 60 ]
```

### Haskell, lens (Canonical)
```haskell
world & each . filtered p . gold +~ 1000
```

### Haskell (Fold)
```haskell
sum [ gold e | e <- world, race e == Nord ]
```

### Haskell (Despawn)
```haskell
filter (not . dead) world
```

### OCaml, fold (Canonical)
```ocaml
List.map (fun e -> if cond e then { e with gold = e.gold + 1000 } else e) world
```

### OCaml, pipe (Fold)
```ocaml
world |> List.filter (fun e -> e.race = Nord) |> List.fold_left (fun a e -> a + e.gold) 0
```

### OCaml, Seq (Fold)
```ocaml
World.to_seq |> Seq.filter is_nord |> Seq.map gold |> Seq.fold_left (+) 0
```

### OCaml (Despawn)
```ocaml
List.filter (fun e -> not e.dead) world
```

### F# (Canonical)
```fsharp
world |> Seq.map (fun e -> if cond e then { e with Gold = e.Gold + 1000 } else e)
```

### Erlang, comprehension + guard (Canonical)
```erlang
[ give(E, 1000) || E <- World, race(E) == nord, two_handed(E) > 60 ].
```

### Erlang qlc (Canonical)
```erlang
qlc:q([ E || E <- mnesia:table(ents), race(E) == nord, two_handed(E) > 60 ]).
```

### Elixir, comprehension (Canonical)
```elixir
for e <- world, e.race == :nord, e.two_handed > 60, do: %{e | gold: e.gold + 1000}
```

### Elixir, Ecto (Canonical)
```elixir
from e in W, where: e.race == ^:nord and e.two_handed > 60, update: [inc: [gold: 1000]]
```

### LINQ, method (Canonical)
```csharp
World.Where(e => e.Race == Nord && e.TwoHanded > 60).ToList().ForEach(e => e.Gold += 1000);
```

### LINQ, query (Canonical)
```csharp
from e in World where e.Race == Nord && e.TwoHanded > 60 select Bump(e)
```

### SQL (Canonical)
```sql
UPDATE ents SET gold = gold + 1000 WHERE race = 'nord' AND twohanded > 60;
```

### SQL (Fold)
```sql
SELECT sum(gold) FROM ents WHERE race = 'nord';
```

### SQL (Despawn)
```sql
DELETE FROM ents WHERE dead;
```

### Datalog (Canonical)
```datalog
master(E) :- race(E, nord), skill(E, twoHanded, V), V > 60.
```
The rule body is the selection. The effect is applied via an extension over the derived predicate `master`.

### Differential dataflow (Canonical)
```rust
ents.filter(|e| e.nord && e.th > 60).map(|e| (e.id, 1000))
```

### Differential dataflow (Despawn)
```rust
ents.filter(|e| !e.dead).consolidate()
```

### Prolog (Canonical)
```prolog
forall((ent(E), race(E, nord), th(E, V), V > 60), add_gold(E, 1000)).
```

### Common Lisp, loop (Canonical)
```lisp
(dolist (e world)
  (when (and (nordp e) (> (two-handed e) 60))
    (incf (gold e) 1000)))
```

### Common Lisp, loop (Fold)
```lisp
(loop for e in world when (nordp e) sum (gold e))
```

### Common Lisp, reduce (Fold)
```lisp
(reduce #'+ (mapcar #'gold (remove-if-not #'nordp world)))
```

### Common Lisp (Despawn)
```lisp
(remove-if #'deadp world)
```

### Clojure, seq (Canonical)
```clojure
(map #(update % :gold + 1000) (filter master? world))
```

### Clojure, thread (Fold)
```clojure
(->> world (filter nord?) (map :gold) (reduce +))
```

### Clojure, transduce (Fold)
```clojure
(transduce (comp (filter nord?) (map :gold)) + 0 world)
```
Transducers fuse filter and map into a single pass with no intermediate allocation, matching the structure of the ano planner's fused column scan.

### Clojure (Despawn)
```clojure
(remove :dead world)
```

### Factor (Canonical)
```factor
world [ master? ] filter [ 1000 + ] map-gold
```

### Lean 4 (Canonical)
```lean
world.filter master |>.map (fun e => { e with gold := e.gold + 1000 })
```

### Agda (Canonical)
```agda
map (λ e → record e { gold = gold e + 1000 }) (filter master world)
```

### Lua (Canonical)
```lua
for _, e in ipairs(world) do
  if master(e) then e.gold = e.gold + 1000 end
end
```

### Starlark (Canonical)
```python
[bump(e) for e in world if e.race == "nord" and e.th > 60]
```

### Egison (Canonical)
```egison
match world (multiset ent)
  { [<cons (& nord (th (& $v ?(> 60)))) _> ...] }
```

---

## Table 2. Candidate ano surfaces

Same canonical task. Each register is a surface over the shared staged-effect core. Variants given per register: Canonical, Fold (sum Nord gold to a scalar), Join (gold to a Nord whose mentor has Two-Handed > 80). All ano code fenced as `haskell` per working convention.

### selector , effect — the original hypothesis (Canonical)
```haskell
Nord & TwoHanded > 60 , Gold += 1000
```

### selector , effect (Fold)
```haskell
+/ Gold @ Nord
```

### selector , effect (Join)
```haskell
Nord & mentor.TwoHanded > 80 , Gold += 1000
```

### qSQL register (Canonical)
```haskell
update Gold += 1000 from Nord where TwoHanded > 60
```

### qSQL register (Fold)
```haskell
select +/Gold from Nord
```

### qSQL register (Join)
```haskell
update Gold += 1000 from Nord where mentor.TwoHanded > 80
```

### qSQL terse (Canonical)
```haskell
Gold+:1000 / Nord, TwoHanded>60
```

### qSQL terse (Fold)
```haskell
+/Gold / Nord
```

### Erlang tuple-match (Canonical)
```haskell
(Nord, TwoHanded > 60) , Gold += 1000
```

### Erlang tuple-match (presence vs value)
```haskell
(Nord, TwoHanded _) , +Trained
```
`TwoHanded _` matches an entity that carries the Two-Handed component with any value. `!TwoHanded` matches an entity lacking the component. Omission of the component imposes no constraint. Component presence corresponds to archetype membership and resolves before any value read.

### Erlang tuple-match (Join)
```haskell
(Nord, mentor:(TwoHanded > 80)) , Gold += 1000
```

### Guard clause (Canonical)
```haskell
give(E) when nord(E), two_handed(E) > 60 -> Gold(E) += 1000
```

### Guard clause (Join)
```haskell
give(E) when nord(E), mentor(E, M), th(M) > 80 -> Gold(E) += 1000
```

### Pipe (Canonical)
```haskell
world |> Nord |> TwoHanded > 60 |> Gold += 1000
```

### Pipe (Fold)
```haskell
world |> Nord |> +/Gold
```

### Pipe + verbs (Canonical)
```haskell
world |> where(Nord & TwoHanded > 60) |> each(Gold += 1000)
```

### Pipe + verbs (Fold)
```haskell
world |> where(Nord) |> sum(Gold)
```

### Monad comprehension (Canonical)
```haskell
[ e.Gold += 1000 | e <- world, Nord e, TwoHanded e > 60 ]
```

### Monad comprehension (Fold)
```haskell
+/ [ Gold e | e <- world, Nord e ]
```

### Monad comprehension (Join)
```haskell
[ e.Gold += 1000 | e <- world, Nord e, m <- mentor e, TwoHanded m > 80 ]
```
The join is expressed as a second generator. The comprehension bind desugars to a relational join.

### Yield comprehension (Canonical)
```haskell
for e in world where Nord, TwoHanded > 60 yield Gold += 1000
```

### Yield comprehension (Join)
```haskell
for e in world, m in mentor e where TwoHanded m > 80 yield Gold += 1000
```

### Rule / Datalog (Canonical)
```haskell
Gold(e) += 1000 <- Nord(e), TwoHanded(e) > 60.
```

### Rule / Datalog (Join)
```haskell
Gold(e) += 1000 <- Nord(e), mentor(e, m), TwoHanded(m) > 80.
```

### Reactive rule (Canonical)
```haskell
rule: when Nord & TwoHanded > 60 -> Gold += 1000
```
Evaluates incrementally on the delta when the body's truth value changes, rather than scanning every frame.

### Reactive rule (Fold)
```haskell
track: +/Gold @ Nord
```

### Optic / point-free (Canonical)
```haskell
world . eachEntity . where(Nord & TwoHanded > 60) . gold %~ +1000
```

### Optic / point-free (Fold)
```haskell
world . eachEntity . where(Nord) . gold & sum
```

### Tacit ASCII (Canonical)
```haskell
(Nord & 60 < TwoHanded) ~> Gold +1000
```

### Tacit ASCII (Join)
```haskell
(Nord & 80 < mentor.TwoHanded) ~> Gold +1000
```

### Array-mask (Canonical)
```haskell
Gold +: 1000 * (Nord & TwoHanded > 60)
```
The selection is a boolean mask. Mutation is a masked add over the column.

### Array-mask (Fold)
```haskell
+/ Nord * Gold
```

### s-expr core, Lisp (Canonical)
```lisp
(each (and Nord (> TwoHanded 60)) (incr Gold 1000))
```
The homoiconic core. Other registers desugar to this form.

### s-expr core, Lisp (Fold)
```lisp
(sum Gold (where Nord))
```

### s-expr core, Lisp (Join)
```lisp
(each (and Nord (> (. mentor TwoHanded) 80)) (incr Gold 1000))
```

### s-expr sugar'd, Lisp + reader macros (Canonical)
```lisp
(give-gold 1000 (& Nord (> TwoHanded 60)))
```

### s-expr sugar'd, Lisp + reader macros (Join)
```lisp
(give-gold 1000 (& Nord (mentor> TwoHanded 80)))
```

### Concatenative (Canonical)
```haskell
Nord [ TwoHanded 60 > ] filter [ Gold 1000 +! ] each
```
Quotations supply the predicate and the effect. No bound names appear. Refactoring requires rewriting the stack flow.

### Set-builder math (Canonical)
```haskell
{ e.Gold += 1000 : e in World, Nord(e), TwoHanded(e) > 60 }
```

### Set-builder math (Fold)
```haskell
Sum{ Gold(e) : Nord(e) }
```

### Match block (Canonical)
```haskell
match Nord & TwoHanded > 60 { Gold += 1000 }
```

### Match block (Fold)
```haskell
fold Nord { +/ Gold }
```

### Selection-as-value (Canonical)
```haskell
let masters = Nord & TwoHanded > 60 in masters.Gold += 1000
```
The selected set is a first-class value, bound to `masters`, then acted upon. Supports passing and re-selection.

### Selection-as-value (Fold)
```haskell
let ns = Nord in +/ ns.Gold
```

### Dataflow DAG (Canonical)
```haskell
richest <- Nord |> maxby Gold ; richest.Gold += 1000
```
A fold result binds to `richest` and scopes a subsequent map. The script forms a two-node dataflow graph. The `<-` binding introduces this capability and requires an intra-script evaluation order.

### Dataflow DAG (Join)
```haskell
disciples <- Nord ⋈ mentor where m.TwoHanded > 80 ; disciples.Gold += 1000
```

### Demonstrative, あの-literal (Canonical)
```haskell
ano Nord, TwoHanded > 60 : Gold += 1000
```
`ano` marks a declarative selection. `dore` marks a query. The two keywords correspond to demonstrative and interrogative deixis. This register places the name in the grammar.

### Demonstrative, あの-literal (Fold)
```haskell
dore +/Gold : Nord
```

### Infix natural (Canonical)
```haskell
every Nord with TwoHanded > 60 gets Gold + 1000
```
Prose-shaped surface with high parsing ambiguity.

### Capability-call (Canonical)
```haskell
Gold.give(1000) @ (Nord & TwoHanded > 60)
```
The verb is a registered capability. The selection following `@` supplies its target scope.

### Capability-call (Fold)
```haskell
Gold.sum() @ Nord
```

### Sigil-mask (Canonical)
```haskell
&Gold +1000 ?[Nord TwoHanded>60]
```
`?[...]` delimits the selector. Sigils prefix the effect target and the columns read.

### OCaml match / guard (Canonical)
```ocaml
match e with _ when nord e && twohanded e > 60 -> { e with gold = gold e + 1000 }
```
The ML `when` guard carries the same selection semantics as the Erlang guard and the Datalog body. The entity is bound to `e` and visible in the effect.

### OCaml match / guard (Join)
```ocaml
match e with _ when nord e && twohanded (mentor e) > 80 -> { e with gold = gold e + 1000 }
```

### OCaml pipe + labelled verbs (Canonical)
```ocaml
world |> select ~p:(Nord & TwoHanded > 60) |> iter ~f:(Gold += 1000)
```

### OCaml pipe + labelled verbs (Fold)
```ocaml
world |> select ~p:Nord |> sum ~f:Gold
```

### OCaml PPX, ano-in-OCaml (Canonical)
```ocaml
[%ano Nord & TwoHanded > 60 => Gold += 1000]
```
ano embedded as a host EDSL via a PPX rewriter. The surface resides in OCaml source, rewrites to staged calls at host build time, and typechecks against the registry as ordinary OCaml. For an OCaml host, the bytecode path is bypassed and the plan is monomorphised at build time.

### OCaml PPX, ano-in-OCaml (Fold)
```ocaml
[%ano +/ Gold @ Nord]
```

---

## Distinguishing axes

Four axes account for the non-cosmetic variation in Table 2.

Statement or expression. The comma sweep, guard, and tuple-match registers evaluate to no value. The comprehension, optic, and selection-as-value registers evaluate to a value, which permits binding the selection, passing it, and re-selecting from it.

Single pass or dataflow graph. The dataflow register permits a fold result to scope a subsequent map within one script. This determines whether the `<-` binding exists, which determines whether the effect buffer requires topological ordering or emission order alone, and whether derived predicates are defined in-language.

Effect position. Leading in the capability-call register. Trailing in the comma sweep. Yielded in the yield comprehension. As a rule head in the rule and reactive registers. The reactive registers surface incremental evaluation in their syntax.

Entity naming. The ML and Lisp registers bind the entity to `e` and reference it in the effect. The q, tacit, and array registers reference no entity name. This axis is the subject of Hsu's "naming is hard" observation. ano references the entity by selection predicate and declares the component vocabulary by name in the registry. The surface carries no entity name; the registry carries the component types.

## Open decisions

The current contenders are the qSQL register, the monad comprehension, selection-as-value, and the あの-literal, with the reactive rule available as an annotation over the chosen base. Three decisions remain open, ordered by how far each propagates.

Per-line sweep or dataflow graph. Determines the existence of `<-`, the buffer ordering, and the EDB/IDB boundary. The q precedent supports a flat sweep as the default with the dataflow graph available through nested composition under a single evaluation model.

EDB/IDB boundary. Whether derived predicates are host-registered natives or ano-defined compositions. The Datalog precedent supports a hybrid: native primitives, in-language derivations, folded into the plan at compile time.

Effect ordering in the buffer. Deterministic replay for rollback netcode requires a total order. Emission-order costs nothing and couples the result to iteration order. Entity-id-order is stable and costs a sort. Commutative set-semantics removes the ordering dependency and constrains which effects may be registered.
