# Behavior trees and NPC authoring

Create a **Behavior Tree** from the asset browser's Create menu. Double-click the
`.bbtree` asset to open the themed block workspace. Nest behaviors in composite
mouths and connect sibling blocks in execution order. Save requires one connected
root and valid child counts. The editor reports incomplete branches without
overwriting the last valid asset. The editor's Undo/Redo actions operate on the
focused document, using the same Qt history routing as shader graphs. Workspace
transactions feed that history; there is no separate block undo list.

Use **Unlock** to author XML directly. The file remains `.bbtree`, with
`locked="false"`. Unlock is undoable. Generated XML is read-only while blocks own
the document; blocks become read-only after Unlock. Hand-authored files can omit
layout attributes. Reload explicitly discards local edits. Saving refuses to
overwrite a file changed externally.

Add a **Behavior Tree Agent** component to an NPC and choose its Tree asset.
Agents run on fixed gameplay updates, before physics simulation. Loop controls
whether terminal trees restart on the next tick. Give NPCs a dynamic Rigidbody
and collider for velocity, force, impulse and MoveTo actions. MoveTo steers
directly toward a world-space blackboard vector through Jolt velocity; it does
not compute a navigation mesh path or avoid obstacles. Raycast and OverlapSphere
exclude the NPC's own body. Raycast stores a boolean and `<key>.point` vector;
OverlapSphere stores a numeric hit count. Physics action failures return Failure.

```xml
<BehaviorTree version="1" locked="false">
  <Blackboard>
    <Vector key="target" x="10" y="1" z="0"/>
    <Number key="investigate" value="0.8"/>
    <Number key="idle" value="0.2"/>
  </Blackboard>
  <Tree>
    <Utility>
      <Option key="investigate" weight="1">
        <Sequence>
          <MoveTo key="target" speed="3" radius="0.3"/>
          <Wait seconds="1"/>
        </Sequence>
      </Option>
      <Option key="idle" weight="1"><Wait seconds="2"/></Option>
    </Utility>
  </Tree>
</BehaviorTree>
```

Blackboards support bool, double, string and Vec3 values. Defaults initialize
missing keys without overwriting values supplied by gameplay code. Each agent
owns its execution state and blackboard. Blackboard declaration types are Bool,
Number, String and Vector. The block catalog also provides SetBool, SetNumber,
SetString, SetVector, AddNumber, HasKey, ClearKey, IsTrue and Compare.

Sequence/Selector retain the running child. Reactive variants reevaluate from
the first child and cancel a preempted branch. Parallel waits for every child,
fails on any failure, and does not retick completed children. Utility reevaluates
`blackboard[key] * weight` each tick, chooses the largest score (first on ties),
and cancels the old branch when the choice changes. Repeat executes at most once
per tick. Timeout cancels its running child. Cooldown returns Failure while its
timer is active. Explicit Reset clears all execution state and cooldowns.

Custom C++ actions and conditions use `AI::Context` callbacks. Access an agent's
memory through `scene.GetSystem<BehaviorTreeSystem>().GetBlackboard(entity)`.
Set the system's Action/Condition callbacks to register gameplay dispatch.
Action callbacks return Success, Failure or Running; their final argument is
true when cancelled. Cancellation should release animation, movement or other
resources the action owns. A standalone `AI::BehaviorTree::FromSource` or
`Load(UUID)` can also be initialized and ticked explicitly with a Context.

XML is bounded to 1 MiB, 64 behavior levels, and 4096 nodes. DTDs and external
entities are rejected. Built-in nodes are validated during asset import; XML
never evaluates source code. Custom behavior dispatch uses Action/Condition
names rather than arbitrary XML tags.

Native callbacks registered through SetHandlers or the system Action/Condition slots retain their gameplay module and inherit the active script owner. Native stop, streamed unload and hot reload detach that owner before destroying script objects. Explicit owner tokens remain supported. Callback invocation restores native module, event, graphics and extension scope. Copies of system slots are callable snapshots. Scene.Clear cancels and removes all agent state and defaults before registry/physics teardown. Agent caches validate Entity generation, including GetBlackboard calls before the next tick. Behavior dependencies include literal String and SetString UUIDs and report Partial coverage for computed/custom action lookups. Numeric parsing reuses the core locale-independent compatibility helper.
