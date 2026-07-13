# Proposal: Clarify IP access-control naming (block vs. allow)

**Status:** Draft — for discussion, no code yet
**Branch:** `feature/v2.0`
**Related:** TASK-029 (IP-control verb collapse), DR-012 (`accept_decision` hook)

---

## 1. Problem

The IP access-control subsystem has **two modes**, selected by
`create_webserver::default_policy(...)`:

| `default_policy` | Meaning | Mental model | Exception list you need |
|------------------|---------|--------------|-------------------------|
| `ACCEPT` (default) | allow everyone *except* listed IPs | **deny-list / blocklist** | the deny list |
| `REJECT` | deny everyone *except* listed IPs | **allow-list / allowlist** | the allow list |

The public verbs are named for **only one** of those modes:

```cpp
void webserver::block_ip(std::string_view ip);    // adds to the deny list
void webserver::unblock_ip(std::string_view ip);  // removes from the deny list
```

`block_ip` reads correctly under `ACCEPT`. Under `REJECT` the operation a user
actually needs is *"permit this IP"* — but the only verb on offer is `block_ip`,
which manipulates the wrong list. The vocabulary silently assumes blocklist
semantics while the config can flip the system into allowlist semantics.

### 1.1 The deeper issue: the allow-list half is dead code

TASK-029 collapsed the historical quartet
(`ban_ip` / `unban_ip` / `allow_ip` / `disallow_ip`) down to
`block_ip` / `unblock_ip`, keeping **only deny-list manipulation** in the public
API. The internal machinery for the allow list was left in place:

- `webserver_impl::allowances` (`std::set<ip_representation>`) + `allowances_mutex`
  — `src/httpserver/detail/webserver_impl.hpp:374`
- the `is_allowed` branch of `classify_decision(...)`
  — `src/detail/webserver_callbacks_lifecycle.cpp:103`
- `policy_callback` still reads `allowances` on every connection
  — `src/detail/webserver_callbacks_lifecycle.cpp:232`
- `default_policy(REJECT)` is a documented, public builder setting
  — `src/httpserver/create_webserver.hpp:303`

**Net effect:** `default_policy(REJECT)` is reachable and documented, but nothing
in the public API can add an IP to `allowances`. In `REJECT` mode the server
rejects *every* connection, permanently. The confusing naming is the surface
symptom; the root cause is a **half-collapsed feature** — an allow-list mode
with no way to fill the allow list.

So the naming decision is downstream of a scope decision:

> **Finish the allow-list mode, or remove it.** Renaming alone cannot fix an API
> that is missing half its surface.

### 1.2 Secondary naming smells

- **`create_webserver::ban_system(bool)`** — the word *ban* implies blocklist,
  but this flag gates the **entire** access-control machinery (both lists, both
  modes). Under `REJECT` it is really "access control on/off", not "ban system".
- **Mismatched internal nouns** — `bans` vs. `allowances` (one is a verb-noun,
  the other an abstract noun; neither says "list").
- **`accept_ctx::reason` strings** — `"banned"` reuses the retired *ban*
  vocabulary; `"not-allowed"` collides visually with HTTP *405 Method Not
  Allowed* elsewhere in the codebase.
- **Precedence is undocumented** — `classify_decision` gives the allow list
  priority over the deny list under `ACCEPT` (`is_banned && !is_allowed` →
  reject). "Allow overrides deny" is a real rule with no name and no doc.

---

## 2. Design goals

1. **One vocabulary** across verbs, enum, flag, internal sets, and reason
   strings. Pick a single antonym pair and use it everywhere.
2. **Both modes first-class** — a user can populate whichever exception list
   their `default_policy` requires.
3. **No `whitelist`/`blacklist`, no `ban`.** Use the modern, unambiguous
   `allow` / `deny` pair (already half-present as `allowances`).
4. **State the precedence rule** ("allow overrides deny") in the name and docs.
5. **Deprecate, don't break** — `feature/v2.0` is unreleased, but keep churn
   contained and provide one release of aliases where cheap.

---

## 3. Options

### Option A — Restore a symmetric, consistently-named API *(recommended)*

Expose both lists with one antonym pair. Finish the allow-list mode.

```cpp
// Deny list — the exception list under default_policy(ACCEPT)
void webserver::deny_ip(std::string_view ip);
void webserver::remove_denied_ip(std::string_view ip);

// Allow list — the exception list under default_policy(REJECT).
// Under ACCEPT, an allow entry overrides a deny entry (allow wins).
void webserver::allow_ip(std::string_view ip);
void webserver::remove_allowed_ip(std::string_view ip);
```

Supporting renames (all with one-release deprecated aliases where public):

| Today | Proposed | Notes |
|-------|----------|-------|
| `block_ip` / `unblock_ip` | `deny_ip` / `remove_denied_ip` | deny-list pair |
| *(missing)* | `allow_ip` / `remove_allowed_ip` | allow-list pair (new public surface) |
| `create_webserver::ban_system(bool)` | `ip_access_control(bool)` | gates the whole subsystem |
| `bans` / `bans_mutex` | `deny_list` / `deny_list_mutex` | internal |
| `allowances` / `allowances_mutex` | `allow_list` / `allow_list_mutex` | internal |
| `accept_ctx::reason == "banned"` | `"denied"` | user-visible string |
| `accept_ctx::reason == "not-allowed"` | `"not-on-allow-list"` | disambiguate from HTTP 405 |
| example `minimal_ip_ban.cpp` | `minimal_ip_access_control.cpp` | docs |

`policy_T { ACCEPT, REJECT }` and `default_policy(...)` can stay — "default =
accept everyone" / "default = reject everyone" reads cleanly and the enum is
used broadly. *Optional:* add value aliases `ALLOW_BY_DEFAULT = ACCEPT`,
`DENY_BY_DEFAULT = REJECT` to make the list connection explicit at call sites.

**Pros:** one vocabulary; both modes usable; deletes the dead-code trap;
self-documenting removal verbs (no `undeny`/`unallow` non-words).
**Cons:** widest surface change; reintroduces a 4-verb API (the thing TASK-029
set out to shrink) — but now *symmetric and named consistently*, which was the
original defect.

*Terser alternative naming* if the `remove_*_ip` pair feels verbose:
`deny_ip`/`undeny_ip`, `allow_ip`/`unallow_ip` (mirrors the existing
`unblock_ip` "un-" prefix, at the cost of two non-words).

### Option B — One policy-neutral verb pair over "the exception list"

Keep a single pair, but make it mean *"the exception to the current default
policy"* and route to whichever list is active:

```cpp
void webserver::add_exception_ip(std::string_view ip);     // deny under ACCEPT, allow under REJECT
void webserver::remove_exception_ip(std::string_view ip);
```

**Pros:** smallest surface; one pair covers both modes; honest about the
symmetry between the two modes.
**Cons:** the *same call* means "block" or "permit" depending on a separate
setting — action-at-a-distance. Loses the ability to seed a deny list *and* an
allow list simultaneously (allow-overrides-deny becomes unexpressible). Not
recommended, but the cleanest fit if we accept "exactly one exception list per
server".

### Option C — Drop allow-list mode entirely; ship a pure deny-list

Accept that v2.0 is deny-list-only. Remove `allowances`, the `is_allowed`
branch, and `default_policy(REJECT)`; keep and clarify one pair:

```cpp
void webserver::deny_ip(std::string_view ip);
void webserver::allow_ip(std::string_view ip);  // = "remove from deny list", NOT an allow list
```

**Pros:** deletes the most code and the dead-code trap; a deny-list-only server
is a coherent, common product.
**Cons:** removes a documented capability (`REJECT`); `allow_ip` meaning
"un-deny" (not "allow-list") is exactly the kind of overload we're trying to
kill — so under Option C, removal should be `undeny_ip`, not `allow_ip`.

---

## 4. Recommendation

**Option A.** `default_policy(REJECT)` is already public and documented, so the
allow list must be reachable — which means completing the API, not shrinking it
further. Option A gives one consistent `allow`/`deny` vocabulary across every
touchpoint, makes both modes usable, documents the allow-overrides-deny
precedence in the method names, and removes the "REJECT rejects everyone"
trap.

If the team decides the allow-list mode is genuinely unwanted, **Option C** is
the honest alternative — but it must also delete `REJECT` and the `allowances`
machinery, not leave them as unreachable code.

Option B is a viable middle path only if we commit to "one exception list per
server" and give up allow-overrides-deny.

---

## 5. Decision → work implied (sketch, no code yet)

- **A:** add `allow_ip` / `remove_allowed_ip`; rename `block_ip`→`deny_ip`
  (+ alias); rename `ban_system`→`ip_access_control` (+ alias); rename internal
  sets + reason strings; add allow-list integ tests (the coverage removed by
  TASK-029, restored under the new names); document precedence.
- **C:** delete `allowances` / `allowances_mutex` / `is_allowed` branch /
  `REJECT` / `default_policy`; rename `block_ip`→`deny_ip`,
  `unblock_ip`→`undeny_ip`; simplify `classify_decision` to a single lookup.

---

## 6. Open questions

1. **Keep `REJECT` mode?** — decides A vs. C. Is anyone relying on
   `default_policy(REJECT)` today, given it is currently non-functional?
2. **Removal-verb style** — self-documenting (`remove_denied_ip`) vs. terse
   (`undeny_ip`)?
3. **Alias horizon** — v2.0 is unreleased; do we ship deprecated aliases at all,
   or rename cleanly since there is no released v2 surface to protect?
4. **Enum** — leave `policy_T { ACCEPT, REJECT }`, or add
   `ALLOW_BY_DEFAULT`/`DENY_BY_DEFAULT` aliases for call-site clarity?
