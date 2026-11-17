# RLV / RLVa in 6|7 World Studio

## Implementation

RLVa **2.5.0** by Kitty Barnett (implementation id 13), reporting RLV API
**3.4.3** (compatible with 2.9.28), inherited from Alchemy. LGPL 2.1.
Source: `indra/newview/rlv*.cpp/h`.

## Enablement and visibility

- **Off by default.** `RestrainedLove` defaults to `0` in
  `app_settings/settings.xml` (Alchemy shipped it on). The user turns it on
  in Preferences; RLVa starts at the next login.
- Active restrictions: **RLVa menu > Restrictions...** (floater
  `rlv_behaviours`, RLVa's own), reachable from **Me > Accessibility (6|7)**
  via "Show active restrictions". The 6|7 floater also states whether RLV is
  off, on but not yet active, or active with _n_ restrictions.

## How 6|7 automation respects RLV

Every automated action goes through `sw67::rlv::Gate`
(`indra/sw67core/include/sw67/rlv/gate.h`) before it runs:

| RLV state | Gate verdict |
|---|---|
| RLV off | Allowed (RLV does not apply) |
| RLV on, not yet initialised this session | **NotReady**: nothing runs (fails closed) |
| RLV on and active | Allowed or **Blocked**, from RLVa's own checks |

`SW67RlvState` (`indra/newview/sw67rlvstate.cpp`) answers from RLVa:

| 6|7 action | RLVa check | Behaviours named in explanations |
|---|---|---|
| Teleport | `RlvActions::canTeleportToLocation` | tploc, tplm, unsit, sittp |
| Local teleport | `canTeleportToLocation` and no `@tplocal` | tplocal, unsit, sittp |
| Sit | `RlvActions::canSit(object)` | sit, sittp, unsit |
| Stand | `RlvActions::canStand` | unsit |
| Wear item | `rlvPredCanWearItem(item, RLV_WEAR)` | addoutfit, addattach, attachthis, attachallthis, (un)sharedwear |
| Take off item | `rlvPredCanRemoveItem(item)` | remoutfit, remattach, detach, detachthis, detachallthis, (un)sharedunwear |
| Move inventory | `RlvFolderLocks::canMoveFolder / canMoveItem` | folder locks |
| Rename / trash inventory | `RlvFolderLocks::canRenameFolder / canRemoveFolder / canRemoveItem` | folder locks |
| Send / start IM | `RlvActions::canSendIM / canStartIM` | sendim, sendimto, startim, startimto |
| Chat (channel 0) | no `@sendchat`, no `@redirchat` | sendchat, redirchat, chatshout, ... |
| Chat (other channel) | `RlvActions::canSendChannel` | sendchannel, sendchannel_except |
| Environment | `RlvActions::canChangeEnvironment` | setenv |
| Camera | `canChangeCameraPreset` and no `@setcam` | setcam, camunlock, camdistmax, ... |
| Touch / Edit / Rez | `canTouch / canEdit / canRez` | touch*, edit*, rez |
| Pay resident / object | `canPayAvatar / canPayObject` | pay, buy |
| Give inventory | `canGiveInventory` | share |
| Snapshot | always allowed: this RLVa has no snapshot behaviour | none |

Unknown items, objects or avatars are treated as **not allowed**.

**Stopping a 6|7 workflow never releases RLV restrictions.** They belong to
the in-world objects that set them. The UI takes its stop message from
`sw67::rlv::describeStop`, which says so whenever restrictions are active.

## Tests

- `indra/sw67core/tests/gate_test.cpp`: gate verdicts (off, not ready,
  blocked with the restriction named, batch preview order, unknown actions,
  stop wording, which actions need explicit authorization). These run in CI
  (`sw67core` workflow) and pass locally.
- The behaviour names the gate uses were checked against RLVa's dictionary
  in `rlvhelper.cpp`.
- **Not yet tested in-world.** The viewer adapter has not run against a live
  RLV device on the Aditi (beta) grid. That is the next RLV step and needs a
  built viewer plus a test account with an RLV-capable object.

## Behaviour names registered in RLVa's dictionary

Extracted from `RlvBehaviourDictionary` in `rlvhelper.cpp` (a few entries are
option words rather than commands). Compatibility with any given RLV device
is not yet tested.

```
acceptpermission accepttp accepttprequest addattach addoutfit addoutfitall 
addoutfitallover addoutfitallthis addoutfitallthisover addoutfitover 
addoutfitthis addoutfitthisover adjustheight allowidle alpha alwaysrun 
attach attachall attachallover attachalloverorreplace attachallthis 
attachallthis_except attachallthisover attachallthisoverorreplace 
attachover attachoverorreplace attachthis attachthis_except attachthisover 
attachthisoverorreplace buy camavdist camdistmax camdistmin camtextures 
camunlock camzoommax camzoommin chatnormal chatshout chatwhisper color 
detach detachall detachallthis detachallthis_except detachme detachthis 
detachthis_except distextend distmax distmin edit editattach editobj 
editworld emote fartouch findfolder findfolders fly getaddattachnames 
getaddoutfitnames getattach getattachnames getcam_avdist getcam_avdistmax 
getcam_avdistmin getcam_fov getcam_fovmax getcam_fovmin getcam_textures 
getcommand getgroup getheightoffset getinv getinvworn getoutfit 
getoutfitnames getpath getpathnew getremattachnames getremoutfitnames 
getsitid getstatus getstatusall interact jump mode notify origin param pay 
permissive recvchat recvchatfrom recvemote recvemotefrom recvim recvimfrom 
redirchat rediremote remattach remoutfit rez sendchannel sendchannel_except 
sendchat sendgesture sendim sendimto setcam setcam_avdist setcam_avdistmax 
setcam_avdistmin setcam_eyeoffset setcam_eyeoffsetscale setcam_focus 
setcam_focusoffset setcam_fov setcam_fovmax setcam_fovmin setcam_mode 
setcam_mouselook setcam_origindistmax setcam_origindistmin setcam_textures 
setcam_unlock setdebug setenv setgroup setoverlay setoverlay_touch 
setoverlay_tween setsphere share sharedunwear sharedwear showhovertext 
showhovertextall showhovertexthud showhovertextworld showinv showloc 
showminimap shownames shownametags shownearby showself showselfhead 
showworldmap sit sitground sittp standtp startim startimto temprun texture 
tint touchall touchattach touchattachother touchattachself touchfar 
touchhud touchme touchthis touchworld tplm tploc tplocal tplure tprequest 
tpto tween unsharedunwear unsharedwear unsit valuemax valuemin version 
versionnew versionnum viewnote viewscript viewtexture viewtransparent 
viewwireframe 
```
