// Input: clicks on the interface and the world, keys, from the window's events (Platform.h).
#include "ClientState.h"
#include "NativeProtocol.h"
#include "TouchControls.h"
#include <cstdlib>

namespace {
void showGameMessage(std::string message);
bool inventoryItemUseable(int32_t id) {
    if(!gameItems)return false;
    for(const auto& item:worldView.items)if(int32_t(item.id)==id) {
        const auto* info=gameItems->find(item.code);
        return info&&info->useable;
    }
    return false;
}
bool isIdentifySource(const d2::ItemSnapshot& item) {
    const auto code=d2::ItemTable::codeString(item.code);
    return code=="isc"||code=="ibk";
}
bool isUnidentified(const d2::ItemSnapshot& item) {
    return (item.flags&0x10)==0&&item.quality>=4&&item.quality<=8;
}
const d2::ItemSnapshot* itemById(int32_t id) {
    for(const auto& item:worldView.items)if(int32_t(item.id)==id)return &item;
    return nullptr;
}
// The server already set IFLAG_TARGETING (packet 0x3F). Packet 0x27 is D2GSPacketClt27: the target, then the scroll.
// Clicking the scroll itself does nothing. An identified item leaves the cursor armed. A missing scroll disarms it.
void identifyTarget(int32_t item) {
    if(!item||!pendingIdentifyItem||item==pendingIdentifyItem)return;
    if(const auto* source=itemById(pendingIdentifyItem);!source||!isIdentifySource(*source)) {
        clearIdentifyCursor();
        showGameMessage("No identify scroll selected.");
        return;
    }
    if(const auto* target=itemById(item);target&&!isUnidentified(*target)) {
        showGameMessage("That item is already identified.");
        return;
    }
    pendingCommands.push_back(d2::Command::raw(0x27|(9<<8),item,pendingIdentifyItem,0,0));
    clearIdentifyCursor();
}
bool backpackHasRoomFor(const d2::ItemSnapshot& item) {
    if(!gameItems)return true;
    const auto* info=gameItems->find(item.code);
    if(!info||info->type=="gold"||info->useable)return true;
    bool occupied[4][10]{};
    for(const auto& other:worldView.items) {
        if(other.mode!=0||other.page!=0||other.vendor)continue;
        const auto* otherInfo=gameItems->find(other.code);if(!otherInfo)continue;
        for(int dy=0;dy<otherInfo->invHeight;++dy)for(int dx=0;dx<otherInfo->invWidth;++dx) {
            const int ox=other.x+dx,oy=other.y+dy;
            if(ox>=0&&ox<10&&oy>=0&&oy<4)occupied[oy][ox]=true;
        }
    }
    for(int y=0;y+info->invHeight<=4;++y)for(int x=0;x+info->invWidth<=10;++x) {
        bool fits=true;
        for(int dy=0;fits&&dy<info->invHeight;++dy)for(int dx=0;dx<info->invWidth;++dx)if(occupied[y+dy][x+dx])fits=false;
        if(fits)return true;
    }
    return false;
}
void showGameMessage(std::string message) {
    gameMessage=std::move(message);gameMessageUntil=worldView.tick+100;
}
// The cursor's item put down with its top left cell at (x,y) of a grid (page 0 backpack, 3 cube, 4 stash), as D2Client 1.10f
// decides (6FAE7AB5-6FAE7EC8). Onto free cells: 0x18. Onto exactly one item, in this order: the two stack (D2Common
// ITEMS_AreStackablesEqual) 0x21; a gem, rune or jewel onto an item with a free socket 0x28; a scroll (item type 22) onto a
// tome (type 18) of the same kind (ITEMS_GetSuffixId 0) 0x29; anything onto the Horadric Cube (code "box ") 0x2A, which
// puts it inside; else the two swap places, 0x1F with the cell. Over two or more items nothing happens.
void putHeldItem(int x,int y,int page,int columns,int rows) {
    const auto* held=itemById(cursorItem);
    if(!held||!gameItems||x<0||y<0||x+cursorItemW>columns||y+cursorItemH>rows)return;
    std::vector<const d2::ItemSnapshot*> under;
    for(const auto& item:worldView.items) {
        if(item.mode!=0||int(item.page)!=page||item.vendor)continue;
        const auto* info=gameItems->find(item.code);if(!info)continue;
        if(item.x<x+cursorItemW&&x<item.x+info->invWidth&&item.y<y+cursorItemH&&y<item.y+info->invHeight)under.push_back(&item);
    }
    if(under.empty()){pendingCommands.push_back(d2::Command::placeItem(cursorItem,x,y,page));return;}
    if(under.size()>1)return;
    const auto& target=*under.front();
    const auto* heldInfo=gameItems->find(held->code);const auto* targetInfo=gameItems->find(target.code);
    const int32_t id=int32_t(target.id);
    if(target.traits&d2::native::itemTraitStacksWithCursor)pendingCommands.push_back(d2::Command::raw(0x21|(9<<8),cursorItem,id,0,0));
    else if((held->traits&d2::native::itemTraitSocketFiller)&&(target.traits&d2::native::itemTraitTakesFiller))pendingCommands.push_back(d2::Command::raw(0x28|(9<<8),cursorItem,id,0,0));
    else if(heldInfo&&targetInfo&&heldInfo->type=="scro"&&targetInfo->type=="book"&&held->suffix==target.suffix)pendingCommands.push_back(d2::Command::raw(0x29|(9<<8),cursorItem,id,0,0));
    else if(d2::ItemTable::codeString(target.code)=="box")pendingCommands.push_back(d2::Command::raw(0x2A|(9<<8),cursorItem,id,0,0));
    else pendingCommands.push_back(d2::Command::raw(0x1F|(17<<8),cursorItem,id,x,y));
}
}

bool storageClick(int mx,int my,bool right) {
    if(!(stashOpen||cubeOpen)||mx<storageX||mx>storageX+storageW||my<storageY||my>storageY+storageH)return false;
    const int columns=stashOpen?6:3,rows=stashOpen?8:4,page=stashOpen?4:3;
    for(const auto& region:storageRegions) {
        // The panel itself (53) comes first in the list; it only keeps clicks from reaching the world, so look past it.
        if(region.kind==53||mx<region.x||mx>region.x+region.w||my<region.y||my>region.y+region.h)continue;
        if((region.kind==51||region.kind==52)&&!right)buttonSound();
        if(region.kind==51&&!right)closeStorage();
        else if(region.kind==52&&!right)pendingCommands.push_back(d2::Command::raw(0x4F|(7<<8),24,0,0,0)); // transmute
        else if(region.kind==50&&region.item&&pendingIdentifyItem&&!right&&!cursorItem)identifyTarget(region.item);
        else if(region.kind==50&&region.item&&right&&!cursorItem&&inventoryItemUseable(region.item))pendingCommands.push_back(d2::Command::useItem(region.item));
        else if(region.kind==50&&!right) {
            if(cursorItem)putHeldItem(std::clamp(region.a-(cursorItemW-1)/2,0,std::max(0,columns-cursorItemW)),std::clamp(region.b-(cursorItemH-1)/2,0,std::max(0,rows-cursorItemH)),page,columns,rows);
            else if(region.item)pendingCommands.push_back(d2::Command::takeItem(region.item));
        }
        break;
    }
    return true;
}
// The left button let go after a press on the belt (D2Client 6FB00E20 -> 6FB012E0): the slot under the cursor takes its item
// onto the cursor (0x24), takes the cursor's item into an empty slot (0x23, the slot) or swaps the two (0x25), the cursor's
// item only if it may go in a belt. Returns whether the release was the belt's.
// A belt item used (0x26: the item, then whether the hireling gets it).
static d2::Command beltUse(int32_t item,bool forHireling){return forHireling?d2::Command::raw(0x26|(13<<8),item,1,0,0):d2::Command::useBelt(item);}
bool beltRelease(int mx,int my) {
    if(!beltPressed)return false;
    beltPressed=false;
    for(const auto& region:beltRegions)if(mx>=region.x&&mx<=region.x+region.w&&my>=region.y&&my<=region.y+region.h){
        if(region.item&&!cursorItem) {
            // Armed identify: an unidentified item in the belt is the target (0x27).
            if(pendingIdentifyItem)if(const auto* item=itemById(region.item);item&&isUnidentified(*item)){identifyTarget(region.item);return true;}
            pendingCommands.push_back(d2::Command::raw(0x24|(5<<8),region.item,0,0,0));
        }
        else if(cursorItem) {
            const auto* held=itemById(cursorItem);
            if(held&&(held->traits&d2::native::itemTraitBeltable))
                pendingCommands.push_back(region.item?d2::Command::raw(0x25|(9<<8),cursorItem,region.item,0,0):d2::Command::raw(0x23|(9<<8),cursorItem,region.b,0,0));
        }
        else beltOpen=!beltOpen;
        return true;
    }
    return true;
}
// A click on the hireling's panel: an item onto or off its body (0x61 with the slot), or the close button.
bool hirelingClick(int mx,int my,bool right) {
    if(!hirelingOpen)return false;
    bool used=false;
    for(const auto& region:hirelingRegions) {
        if(mx<region.x||mx>region.x+region.w||my<region.y||my>region.y+region.h)continue;
        used=true;
        if(right)continue;
        if(region.kind==80&&(cursorItem||region.item))pendingCommands.push_back(d2::Command::raw(0x61|(3<<8),region.a,0,0,0));
        else if(region.kind==81){buttonSound();hirelingOpen=false;}
        if(region.kind!=82)break;
    }
    return used;
}
// A click on the open quest log: a quest shows its description, a tab another act; returns whether it was used.
bool questClick(int mx,int my,bool right) {
    if(!questLog.open)return false;
    bool used=false;
    for(const auto& region:questRegions) {
        if(mx<region.x||mx>region.x+region.w||my<region.y||my>region.y+region.h)continue;
        used=true;
        if(right)continue;
        if(region.kind==70)questLog.selected=region.a;
        else if(region.kind==71){buttonSound();questLog.act=region.a;questLog.selected=0;}
        else if(region.kind==72){buttonSound();questLog.open=false;}
        if(region.kind!=73)break;
    }
    return used;
}
void openGoldPopup(GoldPopup::Kind kind) {
    // The most the game allows (D2Common UNITS_GetInventoryGoldLimit / UNITS_GetStashGoldLimit): 10000 gold per level carried;
    // 50000 per ten levels (to level 30), then 50000 per two levels, in the stash.
    const auto& v=worldView.vitals;
    const int64_t carryLimit=int64_t(10000)*v.level,stashLimit=int64_t(50000)*(v.level<=30?v.level/10+1:v.level/2+1);
    int64_t limit=0;
    switch(kind) {
    case GoldPopup::Kind::Drop:limit=v.gold;break;
    case GoldPopup::Kind::Deposit:limit=std::min<int64_t>(v.gold,std::max<int64_t>(0,stashLimit-v.stashGold));break;
    case GoldPopup::Kind::Withdraw:limit=std::min<int64_t>(v.stashGold,std::max<int64_t>(0,carryLimit-v.gold));break;
    }
    goldPopup={true,kind,int32_t(limit),int32_t(limit),false};
}
void closeGoldPopup(bool accept) {
    const auto popup=goldPopup;goldPopup.open=false;
    if(!accept||popup.amount<=0)return;
    if(popup.kind==GoldPopup::Kind::Drop)pendingCommands.push_back(d2::Command::dropGold(popup.amount));
    else {
        // Packet 0x4F: the stash button (19 withdraw, 20 deposit), then the amount's high and low 16 bits.
        const int32_t button=popup.kind==GoldPopup::Kind::Withdraw?19:20;
        pendingCommands.push_back(d2::Command::raw(0x4F|(7<<8),int32_t(uint32_t(button)|(uint32_t(popup.amount)>>16<<16)),popup.amount&0xFFFF,0,0));
    }
}
// A click inside the open inventory panel: handled here, returns whether it was consumed.
bool inventoryClick(int mx,int my,bool right) {
    if(!inventoryOpen||mx<panelX||mx>panelX+panelW||my<panelY||my>panelY+panelH)return false;
    for(const auto& region:inventoryRegions) {
        if(mx<region.x||mx>region.x+region.w||my<region.y||my>region.y+region.h)continue;
        if(region.kind==1) {
            if(!right&&pendingIdentifyItem&&region.item&&!cursorItem)identifyTarget(region.item);
            else if(!right&&sellToNpc&&region.item&&!cursorItem)pendingCommands.push_back(d2::Command::raw(0x33|(17<<8),sellToNpc,region.item,0,0)); // sold from the grid (item mode 0)
            else if(!right&&repairAtNpc&&region.item&&!cursorItem)pendingCommands.push_back(d2::Command::raw(0x35|(17<<8),repairAtNpc,region.item,0,0)); // repaired alone (0x35, last field 0)
            else if(right){
                // UseItem (0x20) carries the player's position. Identify's prepare sets IFLAG_TARGETING and returns
                // without spending the scroll; town portal has no prepare and casts on this same packet.
                if(region.item&&!cursorItem&&inventoryItemUseable(region.item))pendingCommands.push_back(d2::Command::useItem(region.item));
            }
            else if(cursorItem)putHeldItem(std::clamp(region.a-(cursorItemW-1)/2,0,std::max(0,10-cursorItemW)),std::clamp(region.b-(cursorItemH-1)/2,0,std::max(0,4-cursorItemH)),0,10,4);
            else if(region.item) {
                // Shift with a belt item (potion, scroll) in the backpack sends it to the belt (0x63), D2Client 6FAE7958.
                const auto* item=itemById(region.item);
                const bool shift=scriptedShift||(gameWindow&&gameWindow->keyDown(d2::key::Shift));
                if(shift&&item&&(item->traits&d2::native::itemTraitBeltable)&&item->page==0)pendingCommands.push_back(d2::Command::raw(0x63|(5<<8),region.item,0,0,0));
                else pendingCommands.push_back(d2::Command::takeItem(region.item));
            }
        } else {
            if(pendingIdentifyItem) {
                if(!right&&region.item&&!cursorItem)identifyTarget(region.item);
            } else {
                if(right)break;
                if(repairAtNpc&&region.item&&!cursorItem){pendingCommands.push_back(d2::Command::raw(0x35|(17<<8),repairAtNpc,region.item,0,0));break;}
                if(cursorItem) {
                    // D2Client 1.10f (6FAE6314) switches on D2Common #10299 for the slot (the host's bodyPlacement).
                    const int loc=region.a,slotItem=region.item;
                    switch(loc>=0&&loc<13?worldView.bodyPlacement[size_t(loc)]:0) {
                    case 1:pendingCommands.push_back(d2::Command::equipItem(cursorItem,loc));break;
                    case 2:pendingCommands.push_back(d2::Command::raw(0x1B|(9<<8),cursorItem,loc,0,0));break; // the other hand's item comes to the cursor
                    case 5:pendingCommands.push_back(d2::Command::swapBody(cursorItem,loc));break;
                    case 6:if(slotItem)pendingCommands.push_back(d2::Command::raw(0x21|(9<<8),cursorItem,slotItem,0,0));break; // e.g. arrows onto the quiver
                    case 7:pendingCommands.push_back(d2::Command::raw(0x1E|(9<<8),cursorItem,loc,0,0));break; // two one-handers off for a two-hander
                    default:break; // 0: the game refuses (requirements, wrong slot)
                    }
                }
                else if(region.item)pendingCommands.push_back(d2::Command::takeBody(region.a));
                // An empty hand slot while the other hand holds a two-handed weapon (D2Common #10299 code 4): the original
                // takes the two-hander from the other hand (D2Client 1.10f 0x6FAE611D sends 0x1C with the other slot).
                else if((region.a==4||region.a==5)&&worldView.wieldType==2)pendingCommands.push_back(d2::Command::takeBody(region.a==4?5:4));
            }
        }
        break;
    }
    return true;
}
// A click on the dialog: returns whether it was used.
bool npcClick(int mx,int my,bool shift) {
    if(!npcDialog.open||mx<dialogX||mx>dialogX+dialogW||my<dialogY||my>dialogY+dialogH)return false;
    for(const auto& region:dialogRegions) {
        // The panel itself (23) comes first in the list; it only keeps clicks from reaching the world, so look past it.
        if(region.kind==23||mx<region.x||mx>region.x+region.w||my<region.y||my>region.y+region.h)continue;
        if(region.kind==20||region.kind==21||region.kind==24)buttonSound();
        if(region.kind==20) {
            switch(region.a) {
            case 1:startTalk();break;
            case 9:startIntroduction();break;
            case 2:case 3:
                pendingCommands.push_back(d2::Command::raw(0x2F|(9<<8),1,npcDialog.guid,0,0));
                pendingCommands.push_back(d2::Command::raw(0x38|(13<<8),region.a==2?1:2,npcDialog.guid,0,0));
                npcDialog.page=NpcDialog::Page::Shop;npcDialog.gamble=region.a==3;npcDialog.tab=1;
                if(!inventoryOpen){inventoryOpen=true;npcDialog.openedInventory=true;}
                break;
            case 4:closeNpcDialog();break;
            case 5:finishTalk();break; // a click on the speech skips it
            case 6:pendingCommands.push_back(d2::Command::raw(0x34|(5<<8),npcDialog.guid,0,0,0));closeNpcDialog();break; // Cain identifies everything
            case 8:pendingCommands.push_back(d2::Command::raw(0x62|(5<<8),npcDialog.guid,0,0,0));closeNpcDialog();break; // bring the hireling back (0x62)
            case 7:npcDialog.page=NpcDialog::Page::Hire;break; // the mercenaries on offer (sent as the talk began)
            default:if(region.a>=100&&size_t(region.a-100)<npcDialog.topics.size())startTalk(npcDialog.topics[size_t(region.a-100)]);break; // a quest message's own entry
            }
        } else if(region.kind==25) {
            // Hire this mercenary (packet 0x36, 9 bytes: the NPC, the mercenary's name id as a dword), as D2Client 0x6FAF56D0; the menu closes.
            if(region.a>=0&&size_t(region.a)<worldView.hirelings.size()) {
                buttonSound();
                pendingCommands.push_back(d2::Command::raw(0x36|(9<<8),npcDialog.guid,worldView.hirelings[size_t(region.a)].name,0,0));
                closeNpcDialog();
            }
        } else if(region.kind==24) {
            // Store buttons: buy (the normal way the store works), sell (clicked inventory items are sold), repair everything.
            if(region.a==1) {
                sellToNpc=0;repairAtNpc=0;storeCursor=3;
                if(touch::enabled())showGameMessage("Buying from the store.");
            } else if(region.a==2) {
                sellToNpc=npcDialog.guid;repairAtNpc=0;storeCursor=4;
                if(touch::enabled())showGameMessage("Selling: tap an item in your inventory.");
            } else if(region.a==3)pendingCommands.push_back(d2::Command::raw(0x35|(17<<8),npcDialog.guid,0,0,-1));
            else if(region.a==5){repairAtNpc=repairAtNpc?0:npcDialog.guid;sellToNpc=0;storeCursor=repairAtNpc?1:-1;} // Repair: the next clicked items are repaired one by one
        } else if(region.kind==21)npcDialog.tab=uint32_t(region.a);
        // An item held over the store's goods (dropped there by a click or a drag) is sold, as anywhere on the store (below).
        else if(region.kind==22&&cursorItem&&npcDialog.page==NpcDialog::Page::Shop)pendingCommands.push_back(d2::Command::raw(0x33|(17<<8),npcDialog.guid,cursorItem,4,0));
        else if(region.kind==22&&region.item&&!cursorItem) {
            for(const auto& item:worldView.items)if(int32_t(item.id)==region.item) {
                // Transaction 0 buys from the store, 2 from the gamble offer; shift asks for as many as fit (multibuy bit).
                const uint32_t mode=shift?0x80000000u:0u;
                const uint32_t transaction=(npcDialog.gamble?2u:0u)|mode;
                pendingCommands.push_back(d2::Command::raw(0x32|(17<<8),npcDialog.guid,region.item,int32_t(transaction),int32_t(item.price)));
                boughtItemCode=item.code; // its drop sound plays when the game says it is bought (ClientState, 0x2A)
            }
        }
        return true;
    }
    // Anywhere else on the store with an item on the cursor: sell it.
    if(npcDialog.page==NpcDialog::Page::Shop&&cursorItem)pendingCommands.push_back(d2::Command::raw(0x33|(17<<8),npcDialog.guid,cursorItem,4,0));
    return true;
}
// A click on the waypoint menu: returns whether it was used.
bool waypointClick(int mx,int my) {
    if(!waypointMenu.open||mx<waypointPanelX||mx>waypointPanelX+waypointPanelW||my<waypointPanelY||my>waypointPanelY+waypointPanelH)return false;
    for(auto it=waypointRegions.rbegin();it!=waypointRegions.rend();++it) { // the panel itself is the first region, so test the buttons on top of it first
        const auto& region=*it;
        if(mx<region.x||mx>region.x+region.w||my<region.y||my>region.y+region.h)continue;
        buttonSound();
        if(region.kind==40){pendingCommands.push_back(d2::Command::raw(0x49|(9<<8),waypointMenu.guid,region.a,0,0));waypointMenu=WaypointMenu{};} // the travel ends the use
        if(region.kind==41)closeWaypointMenu();
        if(region.kind==42)waypointMenu.act=region.a;
        break;
    }
    return true;
}
void selectSkill(int skill,bool left) {
    pendingCommands.push_back(d2::Command::raw(0x3C|(9<<8),int32_t(uint32_t(skill)|(left?0x80000000u:0u)),-1,0,0));
}
int skillLevel(int id) {
    for(const auto& skill:worldView.skills)if(int(skill.id)==id)return skill.level;
    return 0;
}
bool canLearnSkill(int id) {
    if(worldView.stats[5]<=0)return false;
    const auto* info=gameSkills?gameSkills->find(uint32_t(id)):nullptr;
    if(!info)return false;
    if(info->requiredLevel>0&&worldView.stats[12]<info->requiredLevel)return false;
    if(info->maxLevel>0&&skillLevel(id)>=info->maxLevel)return false;
    for(const auto req:info->prerequisites)if(req&&skillLevel(int(req))<=0)return false;
    return true;
}
// A mini panel button (also the phone controls' panel buttons): 0 character, 1 inventory, 2 skill tree, 3 party,
// 4 automap, 5 messages, 6 quests, 7 game menu.
void miniPanelButton(int button) {
    if(button==0)characterOpen=!characterOpen;
    else if(button==1){inventoryOpen=!inventoryOpen;if(inventoryOpen)skillsOpen=false;}
    else if(button==2){skillsOpen=!skillsOpen;if(skillsOpen)inventoryOpen=false;}
    else if(button==4)automapOpen=!automapOpen;
    else if(button==6)toggleQuestLog();
    else if(button==7){gameMenuPage=GameMenuPage::Main;gameMenuSelection=0;gameMenuPressed=0;leftHeld=false;}
    else if(button==8) {
        const auto address=d2::lanLocalAddress();
        const char* port=std::getenv("D2MODERN_LAN_HOST");
        gameMessage=address.empty()?"Host: IP unavailable; check Wi-Fi":"Host: "+address+":"+(port&&*port?port:"4000");
        gameMessageUntil=worldView.tick+750;
    }
}
// A click on the skill windows and the two skill boxes; returns whether it was used.
bool skillClick(int mx,int my,bool right) {
    auto inside=[&](const InventoryRegion& region){return mx>=region.x&&mx<=region.x+region.w&&my>=region.y&&my<=region.y+region.h;};
    for(auto it=skillRegions.rbegin();it!=skillRegions.rend();++it) {
        const auto& region=*it;
        if(region.kind==36||!inside(region))continue;
        if(!right&&region.kind!=30&&region.kind!=34)buttonSound(); // buttons and tabs (not the skills themselves)
        switch(region.kind) {
        case 30:if(!right&&canLearnSkill(region.a))pendingCommands.push_back(d2::Command::raw(0x3B|(3<<8),region.a,0,0,0));break;
        case 31:if(!right)skillTab=region.a;break;
        case 32:skillMenu=skillMenu==1?0:1;break;
        case 33:skillMenu=skillMenu==2?0:2;break;
        case 34:if(!touch::assign(region.a))selectSkill(region.a,skillMenu==1);skillMenu=0;break; // or a phone skill button's skill
        case 35:if(!right)pendingCommands.push_back(d2::Command::raw(0x3A|(3<<8),region.a,0,0,0));break;
        case 37:if(!right){runEnabled=!runEnabled;pendingCommands.push_back(d2::Command::setRun(runEnabled));touch::settings().alwaysRun=runEnabled;touch::saveSettings();}break;
        case 38:if(!right)characterOpen=!characterOpen;break;
        // A potion held over the hireling's portrait is given to it (0x61, which D2Game makes the hireling drink: healing,
        // thawing and antidote potions).
        case 47:if(!right&&cursorItem&&worldView.hireling.guid){pendingCommands.push_back(d2::Command::raw(0x61|(3<<8),0,0,0,0));break;}
                if(!right&&worldView.hireling.guid){hirelingOpen=!hirelingOpen;if(hirelingOpen){inventoryOpen=true;skillsOpen=false;characterOpen=false;questLog.open=false;}}break; // the hireling's portrait
        case 39:if(!right){skillsOpen=!skillsOpen;if(skillsOpen)inventoryOpen=false;}break;
        case 42:if(!right)inventoryOpen=false;break;
        case 43:if(!right)skillsOpen=false;break;
        case 48:if(!right)characterOpen=false;break; // the character sheet's close button
        case 44:if(!right)miniPanelOpen=!miniPanelOpen;break;
        case 46:if(!right)pendingCommands.push_back(d2::Command::raw(0x60|(1<<8),0,0,0,0));break; // swap weapon sets
        case 45:if(!right)miniPanelButton(region.a);break;
        default:break;
        }
        return true;
    }
    if(skillMenu){skillMenu=0;return true;}
    for(const auto& region:skillRegions)if(region.kind==36&&inside(region))return true;
    return false;
}
int hoveredSkill(int mx,int my) {
    for(auto it=skillRegions.rbegin();it!=skillRegions.rend();++it) {
        const auto& region=*it;
        if(region.kind!=30&&region.kind!=34&&region.kind!=32&&region.kind!=33)continue;
        if(mx<region.x||mx>region.x+region.w||my<region.y||my>region.y+region.h)continue;
        return region.kind==32?worldView.leftSkill:region.kind==33?worldView.rightSkill:region.a;
    }
    return -1;
}
// Screen position to a scene subtile (native game only).
bool screenToSubtile(int mx,int my,d2::GridPoint& out) {
    if(!walkScene||!gameWindow)return false;
    int w=0,h=0;gameWindow->clientSize(w,h);
    const float x=(mx-w*.5f-panX)/zoom+walkScene->width*.5f,y=(my-h*.5f-panY)/zoom+walkScene->height*.5f;
    out=d2::pickSubtile(*walkScene,x,y);return true;
}
// Clicks on the interface windows, in the order they are drawn on top of each other. Returns whether one used it.
bool uiClick(int mx,int my,bool right) {
    const bool shift=gameWindow&&gameWindow->keyDown(d2::key::Shift);
    if(!right&&npcClick(mx,my,shift))return true;
    if(!right&&waypointClick(mx,my))return true;
    if(!right)for(const auto& region:goldRegions)if(mx>=region.x&&mx<=region.x+region.w&&my>=region.y&&my<=region.y+region.h&&(region.kind==60||region.kind==61)) {
        // The coins: the inventory's drops gold (deposits it while the stash is open), the stash's withdraws it.
        buttonSound();
        openGoldPopup(region.kind==61?GoldPopup::Kind::Withdraw:stashOpen?GoldPopup::Kind::Deposit:GoldPopup::Kind::Drop);
        return true;
    }
    // The belt, as D2Client 1.10f handles it (its belt messages at 6FB6FC1C): the right button, when it goes down, uses the
    // item in the slot (0x26, 6FB01730). The left button going down only notes the press (6FB00CD0); the slot acts when it
    // comes back up (beltRelease).
    for(const auto& region:beltRegions)if(mx>=region.x&&mx<=region.x+region.w&&my>=region.y&&my<=region.y+region.h){
        // With shift held the item goes to the hireling (0x26 with its "on the hireling" word set: D2Client 0x6FB0183B passes
        // the message's MK_SHIFT), which drinks it.
        if(right){if(region.item&&!cursorItem)pendingCommands.push_back(beltUse(region.item,scriptedShift||(gameWindow&&gameWindow->keyDown(d2::key::Shift))));}
        else beltPressed=true;
        return true;
    }
    if(questClick(mx,my,right))return true;
    if(hirelingClick(mx,my,right))return true;
    if(storageClick(mx,my,right))return true;
    if(skillClick(mx,my,right))return true;
    if(inventoryClick(mx,my,right))return true;
    if(characterOpen&&mx>=characterPanelX&&mx<=characterPanelX+characterPanelW&&my>=characterPanelY&&my<=characterPanelY+characterPanelH)return true;
    if(!right&&cursorItem){pendingCommands.push_back(d2::Command::dropItem(cursorItem));return true;}
    return false;
}
// The left button walks, attacks the enemy under it with the left skill, talks to a town person and picks up labelled items; with shift it uses
// the left skill on the spot instead of walking. The right button uses the right skill on the enemy under it or on the spot.
void worldClick(int mx,int my,bool right) {
    if(!walkScene)return;
    const bool shift=gameWindow->keyDown(d2::key::Shift);
    int w=0,h=0;gameWindow->clientSize(w,h);
    const float x=(mx-w*.5f-panX)/zoom+walkScene->width*.5f,y=(my-h*.5f-panY)/zoom+walkScene->height*.5f;
    if(!right) {
        const ItemLabelBox* best=nullptr;float bestDistance=1e30f;
        for(const auto& box:itemLabelBoxes)if(mx>=box.x&&mx<=box.x+box.w&&my>=box.y&&my<=box.y+box.h) {
            const float dx=float(mx)-(box.x+box.w*.5f),dy=float(my)-(box.y+box.h*.5f),distance=dx*dx+dy*dy;
            if(distance<bestDistance){bestDistance=distance;best=&box;}
        }
        if(best){
            if(const auto* item=itemById(best->item);item&&!backpackHasRoomFor(*item))showGameMessage("Your inventory is full.");
            pendingCommands.push_back(d2::Command::pickup(best->item));return;
        }
    }
    if(!right) {
        // Of the objects and entrances under the cursor, the one in front (the one lit up).
        const ObjectPick* front=nullptr;
        for(const auto& box:objectPicks)if(x>=box.x&&x<=box.x+box.w&&y>=box.y&&y<=box.y+box.h&&(!front||box.depth>front->depth))front=&box;
        if(front){
            if(!(front->guid&d2::tileIdBit))if(auto c=objectClasses.find(front->guid);c!=objectClasses.end()&&persistentOpenedContainer(c->second,nullptr))openedObjectModes[front->guid]=2;
            pendingCommands.push_back(d2::Command::interactObject(int32_t(front->guid)));return;
        }
    }
    // A player's corpse: clicking it takes back the gear it holds (0x13 with unit type 0, the body's unit number), the game
    // walking the player there as for an object.
    if(!right)if(const auto* body=pickCorpse(x,y)){pendingCommands.push_back(d2::Command::raw(0x13|(9<<8),0,int32_t((body->id-1000)&~d2::playerIdBit),0,0));return;}
    if(const auto* hit=pickEnemy(x,y)){
        if(right)pendingCommands.push_back(d2::Command::castOn(true,int32_t(hit->id-1000)));
        else pendingCommands.push_back(d2::Command::attack(int32_t(hit->id-1000)));
        return;
    }
    if(!right)if(const auto* person=pickTownPerson(x,y)){pendingCommands.push_back(d2::Command::interact(int32_t(person->id-1000)));return;}
    const auto target=d2::pickSubtile(*walkScene,x,y);
    if(right)pendingCommands.push_back(d2::Command::castAt(true,target));
    else if(shift)pendingCommands.push_back(d2::Command::castAt(false,target));
    else pendingCommands.push_back(d2::Command::moveTo(target));
}
// Holding the left button keeps walking towards the cursor, like the original.
void holdWalk() {
    static uint32_t holdTicks=0;
    if(!leftHeld||!walkScene||cursorItem||pendingIdentifyItem||spellCursor>=0||gameWindow->keyDown(d2::key::Shift)||++holdTicks%6!=0)return;
    int w=0,h=0;gameWindow->clientSize(w,h);
    const float x=(mouseX-w*.5f-panX)/zoom+walkScene->width*.5f,y=(mouseY-h*.5f-panY)/zoom+walkScene->height*.5f;
    if(pickEnemy(x,y)||pickTownPerson(x,y))return;
    d2::GridPoint p;if(screenToSubtile(mouseX,mouseY,p))pendingCommands.push_back(d2::Command::moveTo(p));
}
// The Esc menu's lines (action, caption) on the page shown, top to bottom: the game's three plus Back to Main Menu; the
// options page has the sound, the phone controls and their settings (those only while the controls are on).
std::vector<std::pair<int,std::string>> gameMenuItems() {
    if(gameMenuPage==GameMenuPage::Main) {
        std::vector<std::pair<int,std::string>> items{{1,"OPTIONS"}};
        if(touch::settings().cheats)items.push_back({20,"CHEATS"});
        items.push_back({2,"SAVE AND EXIT GAME"});
        items.push_back({6,"BACK TO MAIN MENU"});
        items.push_back({3,"RETURN TO GAME"});
        return items;
    }
    auto onOff=[](bool on){return std::string(on?"ON":"OFF");};
    if(gameMenuPage==GameMenuPage::Cheats) {
        return {
            {21,"ADD 10,000 GOLD"},
            {22,"ADD 100,000 GOLD"},
            {23,"FILL GOLD"},
            {24,"FILL STASH GOLD"},
            {25,"FILL LIFE AND MANA"},
            {26,"GIVE TOWN PORTAL"},
            {27,"GIVE IDENTIFY SCROLL"},
            {29,"MAX ALL SKILLS"},
            {30,"LEVEL UP"},
            {5,"PREVIOUS MENU"}};
    }
    if(gameMenuPage==GameMenuPage::Sound) {
        const auto& sound=touch::settings();
        return {
            {4,"SOUND: "+onOff(gameAudio&&gameAudio->enabled())},
            {13,"MUSIC: "+onOff(sound.music&&sound.musicVolume>0)},
            {14,"MUSIC VOLUME: "+std::to_string(sound.musicVolume)},
            {15,"SOUND EFFECTS: "+onOff(sound.effects&&sound.effectsVolume>0)},
            {16,"EFFECTS VOLUME: "+std::to_string(sound.effectsVolume)},
            {5,"PREVIOUS MENU"}};
    }
    const auto& sound=touch::settings();
    std::vector<std::pair<int,std::string>> items{
        {17,"SOUND"},
        {7,"TOUCH CONTROLS: "+onOff(touch::enabled())}};
    if(touch::enabled()) {
        static const char* sizes[]={"VERY SMALL","SMALL","NORMAL","LARGE","VERY LARGE"};
        static const char* opacities[]={"FAINT","NORMAL","SOLID"};
        const auto& s=touch::settings();
        items.push_back({8,"LEFT-HANDED: "+onOff(s.leftHanded)});
        items.push_back({9,std::string("BUTTON SIZE: ")+sizes[std::clamp(s.size,0,4)]});
        items.push_back({10,std::string("BUTTONS: ")+opacities[std::clamp(s.opacity,0,2)]});
        items.push_back({11,"AUTO-TARGET: "+onOff(s.autoTarget)});
    }
    items.push_back({12,"VIBRATION: "+onOff(touch::settings().vibration)}); // also a game controller's rumble
    items.push_back({31,"BRIGHTNESS: "+std::to_string(touch::settings().brightness)}); // the original's gamma (Video Options)
    {
        static const char* zooms[]={"1X","1.25X","1.5X","2X","2.5X","3X"};
        items.push_back({19,std::string("VIEW ZOOM: ")+zooms[std::clamp(touch::settings().viewZoom,0,5)]});
    }
#ifdef __ANDROID__
    items.push_back({18,"CHANGE GAME FOLDER"});
#endif
    items.push_back({28,"CHEATS: "+onOff(touch::settings().cheats)});
    items.push_back({5,"PREVIOUS MENU"});
    return items;
}
static void applyGameAudioSettings() {
    if(!gameAudio)return;
    const auto& s=touch::settings();
    gameAudio->setMusicEnabled(s.music&&s.musicVolume>0);
    gameAudio->setMusicVolume(float(s.musicVolume)/10.f);
    gameAudio->setEffectsEnabled(s.effects&&s.effectsVolume>0);
    gameAudio->setEffectsVolume(float(s.effectsVolume)/10.f);
}
void activateGameMenu(int action) {
    auto& s=touch::settings();
    switch(action) {
    case 1:gameMenuPage=GameMenuPage::Options;gameMenuSelection=0;break;
    case 2:gameWindow->close();break; // Orderly NativeGame shutdown invokes D2Game's save callback.
    case 3:gameMenuPage=GameMenuPage::Closed;break;
    case 4:if(gameAudio)gameAudio->setEnabled(!gameAudio->enabled());break;
    case 5:gameMenuPage=gameMenuPage==GameMenuPage::Sound?GameMenuPage::Options:GameMenuPage::Main;gameMenuSelection=0;break;
    // Leaves the game (it is saved as it closes) for the main menu (Main.cpp's gameMain starts the menus again).
    case 6:leaveToMainMenu=true;gameMenuPage=GameMenuPage::Closed;break;
    case 7:touch::setEnabled(!touch::enabled());touch::saveSettings();break;
    case 8:s.leftHanded=!s.leftHanded;touch::reset();touch::saveSettings();break;
    case 9:s.size=(s.size+1)%5;touch::saveSettings();break;
    case 10:s.opacity=(s.opacity+1)%3;touch::saveSettings();break;
    case 11:s.autoTarget=!s.autoTarget;touch::saveSettings();break;
    case 12:s.vibration=!s.vibration;touch::saveSettings();if(s.vibration&&gameWindow)gameWindow->vibrate(.6f,150);break; // a shake to show it works
    case 13:s.music=!s.music;applyGameAudioSettings();touch::saveSettings();break;
    case 14:s.musicVolume=(s.musicVolume+1)%11;s.music=s.musicVolume>0;applyGameAudioSettings();touch::saveSettings();break;
    case 15:s.effects=!s.effects;applyGameAudioSettings();touch::saveSettings();break;
    case 16:s.effectsVolume=(s.effectsVolume+1)%11;s.effects=s.effectsVolume>0;applyGameAudioSettings();touch::saveSettings();break;
    case 17:gameMenuPage=GameMenuPage::Sound;gameMenuSelection=0;break;
    case 31:s.brightness=(s.brightness+1)%11;touch::saveSettings();break;
#ifdef __ANDROID__
    case 18:d2::forgetSharedGameFolder();break;
#endif
    case 19:
        s.viewZoom=(s.viewZoom+1)%6;
        zoom=initialZoom=touch::viewZoomScale();
        touch::saveSettings();
        break;
    case 20:gameMenuPage=GameMenuPage::Cheats;gameMenuSelection=0;break;
    case 21:pendingCommands.push_back(d2::Command::cheat(0,10000));showGameMessage("Added 10,000 gold.");break;
    case 22:pendingCommands.push_back(d2::Command::cheat(0,100000));showGameMessage("Added 100,000 gold.");break;
    case 23:pendingCommands.push_back(d2::Command::cheat(0,0));showGameMessage("Filled carried gold.");break;
    case 24:pendingCommands.push_back(d2::Command::cheat(1,0));showGameMessage("Filled stash gold.");break;
    case 25:pendingCommands.push_back(d2::Command::cheat(2));showGameMessage("Filled life and mana.");break;
    case 26: {
        uint32_t packed=0x20202020;const char* code="tsc";
        for(size_t i=0;code[i]&&i<4;++i)packed=(packed&~(0xFFu<<(8*i)))|(uint32_t(uint8_t(code[i]))<<(8*i));
        pendingCommands.push_back(d2::Command::cheat(3,int32_t(packed),2));
        showGameMessage("Gave a Town Portal scroll.");
        break;
    }
    case 27: {
        uint32_t packed=0x20202020;const char* code="isc";
        for(size_t i=0;code[i]&&i<4;++i)packed=(packed&~(0xFFu<<(8*i)))|(uint32_t(uint8_t(code[i]))<<(8*i));
        pendingCommands.push_back(d2::Command::cheat(3,int32_t(packed),2));
        showGameMessage("Gave an Identify scroll.");
        break;
    }
    case 28:s.cheats=!s.cheats;touch::saveSettings();break;
    case 29:pendingCommands.push_back(d2::Command::cheat(4));showGameMessage("Maxed all class skills.");break;
    case 30:pendingCommands.push_back(d2::Command::cheat(5));showGameMessage("Gained a level.");break;
    default:break;
    }
    gameMenuSelection=std::clamp(gameMenuSelection,0,int(gameMenuItems().size())-1);
    gameMenuPressed=0;
}
// One event from the window (Platform.h), in the order things are on screen: the menus while they are up, the game menu,
// then the game.
void handleInput(const d2::InputEvent& e) {
    using T=d2::InputEvent::Type;
    const bool fresh=!e.repeat; // a key just pressed, not held down
    if(e.type==T::Shot){shotRequest=e.shot;return;}
    if(e.type==T::TouchDown||e.type==T::TouchMove||e.type==T::TouchUp||e.type==T::PadButton||e.type==T::PadAxis){touch::handle(e);return;} // fingers and game controllers (TouchControls.cpp)
    if(e.type==T::ButtonDown&&touch::dismissComparison())return;
    if(e.type==T::ButtonDown&&(e.button==0||e.button==1))cursorPressed=nowMilliseconds();
    if(e.type==T::ButtonDown&&e.button==0)leftButtonDown=true;
    if(e.type==T::ButtonUp&&e.button==0)leftButtonDown=false;
    if(e.type==T::ButtonDown&&e.button==1)rightButtonDown=true;
    if(e.type==T::ButtonUp&&e.button==1)rightButtonDown=false;
    if(e.type==T::MouseMove&&frontEnd){mouseX=e.x;mouseY=e.y;}
    if(frontEnd) {
        const float mx=(e.x-menuLeft)/menuScale,my=(e.y-menuTop)/menuScale;
        if(e.type==T::MouseMove){frontEnd->mouseMove(mx,my);return;}
        if(e.type==T::ButtonDown&&e.button==0){
            if((frontEnd->nameFieldAt(mx,my)||frontEnd->addressFieldAt(mx,my))&&gameWindow) {
                gameWindow->setTextInput(false);
                gameWindow->setTextInput(true);
            }
            frontEnd->mouseDown(mx,my);return;
        }
        if(e.type==T::ButtonUp&&e.button==0){frontEnd->mouseUp(mx,my);return;}
        if(e.type==T::Wheel){frontEnd->wheel(e.wheel);return;}
        if(e.type==T::Key){if(fresh)frontEnd->key(e.key);return;}
        if(e.type==T::Text){frontEnd->character(e.text);return;}
        if(e.type==T::ButtonDown&&e.button!=0)return;
    }
    if(gameMenuPage!=GameMenuPage::Closed) {
        if(e.type==T::MouseMove) {
            mouseX=e.x;mouseY=e.y;
            for(size_t i=0;i<gameMenuRegions.size();++i){const auto& r=gameMenuRegions[i];if(mouseX>=r.x&&mouseX<=r.x+r.w&&mouseY>=r.y&&mouseY<=r.y+r.h)gameMenuSelection=int(i);}
            return;
        }
        if(e.type==T::ButtonDown&&e.button==0) {
            gameMenuPressed=0;
            for(const auto& r:gameMenuRegions)if(e.x>=r.x&&e.x<=r.x+r.w&&e.y>=r.y&&e.y<=r.y+r.h){gameMenuPressed=r.action;break;}
            return;
        }
        if(e.type==T::ButtonUp&&e.button==0) {
            int action=0;
            for(const auto& r:gameMenuRegions)if(e.x>=r.x&&e.x<=r.x+r.w&&e.y>=r.y&&e.y<=r.y+r.h){action=r.action;break;}
            if(action&&action==gameMenuPressed)activateGameMenu(action);else gameMenuPressed=0;
            return;
        }
        if(e.type==T::Key&&fresh) {
            if(e.key==d2::key::Escape){
                if(gameMenuPage==GameMenuPage::Sound){gameMenuPage=GameMenuPage::Options;gameMenuSelection=0;}
                else if(gameMenuPage==GameMenuPage::Options||gameMenuPage==GameMenuPage::Cheats){gameMenuPage=GameMenuPage::Main;gameMenuSelection=0;}
                else gameMenuPage=GameMenuPage::Closed;
                return;
            }
            const auto items=gameMenuItems();const int n=int(items.size());
            if(e.key==d2::key::Up||e.key==d2::key::Down){gameMenuSelection=(gameMenuSelection+(e.key==d2::key::Down?1:n-1))%n;return;}
            if(e.key==d2::key::Enter){activateGameMenu(items[size_t(gameMenuSelection%n)].first);return;}
        }
        if(e.type==T::Key)return;
        if((e.type==T::ButtonDown&&e.button!=0)||e.type==T::Wheel||(e.type==T::ButtonUp&&e.button==2)||e.type==T::Text)return;
    }
    if(goldPopup.open) {
        // The gold popup has the keys (digits, Backspace, Enter, Esc) and the clicks until it closes.
        if(e.type==T::Text&&e.text>='0'&&e.text<='9') {
            const int64_t typed=(goldPopup.typed?int64_t(goldPopup.amount)*10:0)+int64_t(e.text-'0');
            goldPopup.amount=int32_t(std::min<int64_t>(typed,goldPopup.limit));goldPopup.typed=true;return;
        }
        if(e.type==T::Key) {
            if(e.key==d2::key::Escape)closeGoldPopup(false);
            else if(e.key==d2::key::Enter)closeGoldPopup(true);
            else if(e.key==d2::key::Backspace){goldPopup.amount/=10;goldPopup.typed=true;}
            return;
        }
        if(e.type==T::ButtonDown&&e.button==0) {
            for(const auto& region:goldRegions)if(e.x>=region.x&&e.x<=region.x+region.w&&e.y>=region.y&&e.y<=region.y+region.h&&(region.kind==62||region.kind==63)){buttonSound();closeGoldPopup(region.kind==62);break;}
            return;
        }
        if(e.type==T::ButtonDown||e.type==T::Wheel||e.type==T::Text)return;
    }
    if(e.type==T::Wheel){zoom=std::clamp(zoom*(e.wheel>0?1.25f:0.8f),0.02f,16.0f);return;}
    if(e.type==T::ButtonDown&&e.button==2){dragX=e.x;dragY=e.y;dragging=true;gameWindow->captureMouse(true);return;}
    if(e.type==T::ButtonUp&&e.button==2){dragging=false;gameWindow->captureMouse(false);return;}
    if(e.type==T::ButtonDown&&e.button==0) {
        if(!walkGrid){dragX=e.x;dragY=e.y;dragging=true;gameWindow->captureMouse(true);return;}
        // Drag and drop: a press with nothing on the cursor that picks an item up arms a drop where the button comes up.
        const bool emptyCursor=cursorItem==0;const size_t before=pendingCommands.size();
        if(uiClick(e.x,e.y,false)) {
            itemDrag=ItemDrag{};
            if(emptyCursor&&!pendingIdentifyItem&&!sellToNpc&&!repairAtNpc&&pendingCommands.size()>before)itemDrag={true,e.x,e.y,0,0,0};
            return;
        }
        if(pendingIdentifyItem||spellCursor>=0)return;
        leftHeld=true;worldClick(e.x,e.y,false);return;
    }
    if(e.type==T::ButtonUp&&e.button==0) {
        if(!walkGrid){dragging=false;gameWindow->captureMouse(false);}leftHeld=false;beltRelease(e.x,e.y);
        // Released away from where the item was picked up: dropped there, as a click would put it (onto the store sells it,
        // onto the grid, the stash or a body slot places it). Over the world nothing happens: it stays on the cursor.
        if(itemDrag.armed) {
            itemDrag.armed=false;
            const int dx=e.x-itemDrag.fromX,dy=e.y-itemDrag.fromY;
            if(dx*dx+dy*dy>8*8) {
                if(cursorItem)uiClick(e.x,e.y,false);
                else {itemDrag.waiting=true;itemDrag.dropX=e.x;itemDrag.dropY=e.y;itemDrag.since=nowMilliseconds();}
            }
        }
        return;
    }
    if(e.type==T::MouseMove){mouseX=e.x;mouseY=e.y;}
    if(e.type==T::MouseMove&&dragging){panX+=e.x-dragX;panY+=e.y-dragY;dragX=e.x;dragY=e.y;followPlayer=false;return;}
    if(e.type==T::ButtonDown&&e.button==1&&walkGrid) {
        if(uiClick(e.x,e.y,true))return;
        worldClick(e.x,e.y,true);return;
    }
    if(e.type==T::Key) {
        const unsigned k=e.key;
        if(k>=d2::key::F1&&k<=d2::key::F8&&fresh&&walkGrid) {
            const int slot=int(k-d2::key::F1);
            if(const int skill=hoveredSkill(mouseX,mouseY);skill>=0)skillHotkeys[size_t(slot)]=skill;
            else if(skillHotkeys[size_t(slot)])selectSkill(skillHotkeys[size_t(slot)],false);
            return;
        }
        if(k==d2::key::Escape&&fresh&&worldView.vitals.maxLife>0&&worldView.vitals.life<=0){if(characterHardcore)leaveToMainMenu=true;else pendingCommands.push_back(d2::Command::respawn());return;} // dead: back to town, as "Press ESC to continue." says; a hardcore character is gone: out of the game
        if(k==d2::key::Escape&&touch::dismissComparison())return;
        if(k==d2::key::Escape){if(pendingIdentifyItem||spellCursor>=0){if(pendingIdentifyItem)pendingCommands.push_back(d2::Command::raw(0x27|(9<<8),pendingIdentifyItem,pendingIdentifyItem,0,0));clearIdentifyCursor();}else if(stashOpen||cubeOpen)closeStorage();else if(npcDialog.open)closeNpcDialog();else if(waypointMenu.open)closeWaypointMenu();else if(skillMenu)skillMenu=0;else if(skillsOpen)skillsOpen=false;else if(questLog.open)questLog.open=false;else if(hirelingOpen)hirelingOpen=false;else if(characterOpen)characterOpen=false;else if(inventoryOpen)inventoryOpen=false;else if(walkGrid){gameMenuPage=GameMenuPage::Main;gameMenuSelection=0;gameMenuPressed=0;leftHeld=false;}else gameWindow->close();}
        if(k==d2::key::Right) ++frameStep;
        if(k==d2::key::Left) --frameStep;
        if(k=='R'&&fresh) {
            if(worldView.vitals.maxLife>0){runEnabled=!runEnabled;pendingCommands.push_back(d2::Command::setRun(runEnabled));touch::settings().alwaysRun=runEnabled;touch::saveSettings();}
            else {zoom=initialZoom;panX=panY=0;}
        }
        if(k==d2::key::F3&&fresh)showCollision=!showCollision;
        if(k=='C'&&fresh&&walkGrid){characterOpen=!characterOpen;if(characterOpen)questLog.open=false;}
        if(k=='Q'&&fresh&&walkGrid)toggleQuestLog();
        if(k=='O'&&fresh&&walkGrid&&worldView.hireling.guid){hirelingOpen=!hirelingOpen;if(hirelingOpen){inventoryOpen=true;skillsOpen=false;characterOpen=false;questLog.open=false;}}
        if(k=='W'&&fresh&&walkGrid&&worldView.vitals.maxLife>0&&expansionGame)pendingCommands.push_back(d2::Command::raw(0x60|(1<<8),0,0,0,0)); // swap weapon sets
        if(k=='T'&&fresh&&walkGrid){skillsOpen=!skillsOpen;if(skillsOpen)inventoryOpen=false;}
        if(k=='F')followPlayer=true;
        if(k=='I'&&fresh){inventoryOpen=!inventoryOpen;if(inventoryOpen)skillsOpen=false;}
        if(k==d2::key::Grave&&fresh&&walkGrid)beltOpen=!beltOpen;
        if(k==d2::key::Tab&&fresh&&walkGrid)automapOpen=!automapOpen;
        if(k>='1'&&k<='4'&&fresh)if(const auto item=beltItem(int(k-'1')))pendingCommands.push_back(beltUse(item,scriptedShift||gameWindow->keyDown(d2::key::Shift))); // shift: for the hireling
    }
}
