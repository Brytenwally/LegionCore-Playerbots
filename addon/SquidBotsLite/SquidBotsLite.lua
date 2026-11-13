-- SquidBots Lite for DemonCore 7.3.5 (Legion), port of the SkyFire 5.4.8 version: a thin bar of orders
-- (a Revive button joins it while a bot lies dead), the bots' roles drawn on the game's own party frames,
-- and My alts to log your other characters in as bots.
-- Everything it sends is a chat order DemonCore-Playerbots understands (PlayerbotAI::HandleOrder: follow, stay,
-- summon, attack, passive, assist, co / nc, player tank / bot tank, talents, autogear, maintenance, sell,
-- quests); My alts and the GM items use the GM-only ".playerbots add / remove / list" commands.

SquidBotsLiteDB = SquidBotsLiteDB or {}

local STRINGS = {
	en = {
		FOLLOW = "Follow", STAY = "Stay", ATTACK = "Target", PASSIVE = "Passive", ACTIVE = "Active", DUNGEON = "Dungeon",
		ALTS_BAR = "Alts", RECRUIT = "Bots", SUMMON = "Regroup", RELEASE = "Revive",
		TIP_FOLLOW = "Bots follow you.", TIP_STAY = "Bots stay where they are.", TIP_ATTACK = "Bots attack your target.",
		TIP_ACTIVE = "Bots fight normally. Click to make them passive: they only fight back when attacked.",
		TIP_PASSIVE = "Bots are passive: they only fight back. Click, or give any order, to wake them.",
		TOGGLE_PASSIVE = "Passive / Active",
		TIP_DUNGEON = "Dungeon mode: bots stick to you, never pull on their own and help with whatever attacks the group.",
		TIP_RECRUIT = "Group orders: best gear, maintenance (spells, talents, mounts, repair), sell grey items, quests nearby. And My alts.",
		TIP_ALTS = "Your other characters: log them in as bots, summon them, log them out (needs GM rights).",
		TIP_SUMMON = "Bots teleport next to you. Handy when one is stuck.",
		TIP_RELEASE = "Shown when a bot is dead: \"summon\" in party chat brings the bots to you and revives the dead ones (out of combat).",
		TIP_MOVE = "Shift-drag to move the bar, Ctrl + mouse wheel to resize it.",
		TIP_PROFILES = "Right click: profiles (farm, strict follow, dungeon) and orders by role.",
		PROFILES = "Profiles", P_FARM = "Farm (hunt, loot, eat)", P_STRICT = "Strict follow",
		P_DUNGEON = "Dungeon mode",
		GEAR = "Best gear for the group", SPELLS = "Maintenance: spells, talents, mounts, repair (group)", SELL = "Sell grey items at a vendor nearby (group)",
		QUESTS = "Quests the nearest quest giver offers (group)", ACTIVE_BOTS = "List the active bots (GM)",
		GM_ON = "GM mode on: GM items (active bots, log out) show in the menus.",
		GM_OFF = "GM mode off.",
		NOT_IN_GROUP = "You are not in a group with bots.",
		M_FOLLOW = "Follow me", M_STAY = "Stay here", M_ATTACK = "Attack my target", M_SUMMON = "Come to me",
		M_GEAR = "Best gear", M_SPELLS = "Maintenance (spells, talents, mounts, repair)", M_SELL = "Sell grey items at a vendor nearby", M_KICK = "Remove from group",
		M_MAKE_LEADER = "Make it the leader", M_PLAYER_TANK = "I tank: it fights as a damage dealer", M_BOT_TANK = "It tanks: taunts what hits the group",
		M_SPEC = "Switch specialization", M_LOGOUT = "Log out (GM)",
		ROLE_TANK = "Tank", ROLE_HEAL = "Healer", ROLE_DPS = "Damage",
		ROLE_ORDERS = "Orders by role", R_TANK_ATTACK = "Tank: attack my target", R_DPS_ATTACK = "Damage: attack my target",
		R_HEAL_FOLLOW = "Healers: follow me", R_HEAL_STAY = "Healers: stay here", R_RANGED_FOLLOW = "Ranged: follow me",
		TIP_BOT_ICON = "Click: orders for this bot.",
		ALTS = "My alts", ALTS_REFRESH = "Refresh", ALTS_ADD = "Log in", ALTS_REMOVE = "Log out", ALTS_SUMMON = "Summon",
		ALTS_LEVEL = "Level %d %s", ALTS_UNKNOWN = "Level ?",
		TEAMS = "Teams", T_LOGIN = "Log in (%d)", T_LOGOUT = "Log out", T_DELETE = "Delete", T_SAVE_GROUP = "Save my group as a team...",
		T_SAVE_ONLINE = "Save the logged-in alts as a team...", T_NAME = "Name of the team:", T_NONE = "No team yet.",
		T_SAVED = "Team %s saved: %d characters.", T_STARTED = "Team %s: logging in %d characters, one a second.",
		T_EMPTY = "Nobody to save: invite or log in your alts first.",
		ALTS_WAIT = "Asking the server...",
		ALTS_EMPTY = "No other character known yet: log in once on each of your characters with the addon on.",
		ALTS_NO_REPLY = "No answer from the server: .playerbots list needs GM rights.",
		ALTS_HELP = "Log in uses .playerbots add (GM). A logged-in alt is invited to your group.",
		ALTS_COUNT = "%d / %d logged in", TIP_ALTS_REFRESH = "Ask the server which bots are logged in.",
		TIP_ALT_ADD = "Logs it in as a bot and invites it to your group.", TIP_ALT_REMOVE = "Logs it out of the game.",
		TIP_ALT_SUMMON = "Brings it to you: an alt logs in where it logged out.",
		ALTS_FORGET = "Right click: forget this character.", ALTS_FORGOT = "%s is no longer listed in My alts.",
		ALTS_QUESTS = "Quests", TIP_ALT_QUESTS = "The quests it is on, with their objectives (quests all).",
		M_QUESTS = "Its quests", Q_TITLE = "Quests of %s", Q_WAIT = "Asking %s...", Q_NO_REPLY = "No answer from %s.", Q_OTHER = "A quest list came from \"%s\", not from %s: please report this name.",
		Q_INCOMPLETE = "In progress", Q_COMPLETE = "Ready to turn in", Q_REFRESH = "Ask again",
		Q_HELP = "The quests in progress with their objectives, then the ones ready to turn in.",
		ROLES_ON = "Automatic roles on: a member without a party role is inspected for its specialization.",
		ROLES_OFF = "Automatic roles off.",
		RAIDFRAMES_ON = "Role icons and bot menus on the raid-style frames: on.",
		RAIDFRAMES_OFF = "Role icons and bot menus on the raid-style frames: off.",
		HELP = "SquidBots Lite: /sbl (show/hide), /sbl show, /sbl hide, /sbl scale 0.5-2, /sbl lang fr|en, /sbl alts, /sbl quests <name>, /sbl roles on|off, /sbl raidframes on|off, /sbl gm, /sbl reset. Key bindings: Esc > Key Bindings > SquidBots Lite.",
		SCALE_SET = "SquidBots Lite bar size: %s (this character).", SCALE_BAD = "Bar size: a number from 0.5 to 2, for example /sbl scale 1.2",
		LANG_SET = "SquidBots Lite language: English.",
	},
	fr = {
		FOLLOW = "Suivre", STAY = "Rester", ATTACK = "Cible", PASSIVE = "Passifs", ACTIVE = "Actifs", DUNGEON = "Donjon",
		ALTS_BAR = "Alts", RECRUIT = "Bots", SUMMON = "Regrouper", RELEASE = "Relever",
		TIP_FOLLOW = "Les bots vous suivent.", TIP_STAY = "Les bots restent sur place.", TIP_ATTACK = "Les bots attaquent votre cible.",
		TIP_ACTIVE = "Les bots combattent normalement. Cliquez pour les rendre passifs : ils ne font que riposter.",
		TIP_PASSIVE = "Les bots sont passifs : ils ne font que riposter. Cliquez, ou donnez n'importe quel ordre, pour les réveiller.",
		TOGGLE_PASSIVE = "Passifs / Actifs",
		TIP_DUNGEON = "Mode donjon : les bots restent près de vous, n'engagent jamais seuls et aident contre tout ce qui attaque le groupe.",
		TIP_RECRUIT = "Ordres au groupe : meilleur équipement, entretien (sorts, talents, montures, réparation), vente des objets gris, quêtes proches. Et Mes alts.",
		TIP_ALTS = "Vos autres personnages : les connecter en bots, les invoquer, les déconnecter (droits MJ requis).",
		TIP_SUMMON = "Les bots se téléportent à côté de vous. Pratique quand l'un d'eux est coincé.",
		TIP_RELEASE = "Affiché quand un bot est mort : \"summon\" dans le chat de groupe amène les bots à vous et relève les morts (hors combat).",
		TIP_MOVE = "Maj + glisser pour déplacer la barre, Ctrl + molette pour la redimensionner.",
		TIP_PROFILES = "Clic droit : profils (farm, suivi strict, donjon) et ordres par rôle.",
		PROFILES = "Profils", P_FARM = "Farm (chasse, butin, repas)", P_STRICT = "Suivi strict",
		P_DUNGEON = "Mode donjon",
		GEAR = "Meilleur équipement pour le groupe", SPELLS = "Entretien : sorts, talents, montures, réparation (groupe)",
		SELL = "Vendre les objets gris chez un marchand proche (groupe)", QUESTS = "Quêtes du donneur de quêtes le plus proche (groupe)",
		ACTIVE_BOTS = "Lister les bots actifs (MJ)",
		GM_ON = "Mode MJ activé : les options MJ (bots actifs, déconnexion) apparaissent dans les menus.",
		GM_OFF = "Mode MJ désactivé.",
		NOT_IN_GROUP = "Vous n'êtes pas dans un groupe avec des bots.",
		M_FOLLOW = "Suis-moi", M_STAY = "Reste ici", M_ATTACK = "Attaque ma cible", M_SUMMON = "Viens à moi",
		M_GEAR = "Meilleur équipement", M_SPELLS = "Entretien (sorts, talents, montures, réparation)", M_SELL = "Vendre les objets gris chez un marchand proche",
		M_KICK = "Retirer du groupe", M_MAKE_LEADER = "En faire le chef", M_PLAYER_TANK = "Je tanke : il combat comme un DPS",
		M_BOT_TANK = "Il tanke : provoque ce qui frappe le groupe",
		M_SPEC = "Changer de spécialisation", M_LOGOUT = "Déconnecter (MJ)",
		ROLE_TANK = "Tank", ROLE_HEAL = "Soigneur", ROLE_DPS = "Dégâts",
		ROLE_ORDERS = "Ordres par rôle", R_TANK_ATTACK = "Tank : attaque ma cible", R_DPS_ATTACK = "Dégâts : attaquez ma cible",
		R_HEAL_FOLLOW = "Soigneurs : suivez-moi", R_HEAL_STAY = "Soigneurs : restez ici", R_RANGED_FOLLOW = "Distance : suivez-moi",
		TIP_BOT_ICON = "Clic : ordres pour ce bot.",
		ALTS = "Mes alts", ALTS_REFRESH = "Actualiser", ALTS_ADD = "Connecter", ALTS_REMOVE = "Déconnecter", ALTS_SUMMON = "Invoquer",
		ALTS_LEVEL = "Niveau %d %s", ALTS_UNKNOWN = "Niveau ?",
		TEAMS = "Équipes", T_LOGIN = "Connecter (%d)", T_LOGOUT = "Déconnecter", T_DELETE = "Supprimer",
		T_SAVE_GROUP = "Enregistrer mon groupe comme équipe...", T_SAVE_ONLINE = "Enregistrer les alts connectés comme équipe...",
		T_NAME = "Nom de l'équipe :", T_NONE = "Aucune équipe pour l'instant.",
		T_SAVED = "Équipe %s enregistrée : %d personnages.", T_STARTED = "Équipe %s : connexion de %d personnages, un par seconde.",
		T_EMPTY = "Personne à enregistrer : invitez ou connectez d'abord vos alts.",
		ALTS_WAIT = "Question au serveur...",
		ALTS_EMPTY = "Aucun autre personnage connu : connectez-vous une fois sur chacun de vos personnages avec l'addon activé.",
		ALTS_NO_REPLY = "Pas de réponse du serveur : .playerbots list demande les droits MJ.",
		ALTS_HELP = "Connecter utilise .playerbots add (MJ). Un alt connecté est invité dans votre groupe.",
		ALTS_COUNT = "%d / %d connectés", TIP_ALTS_REFRESH = "Demander au serveur quels bots sont connectés.",
		TIP_ALT_ADD = "Le connecte en bot et l'invite dans votre groupe.", TIP_ALT_REMOVE = "Le déconnecte du jeu.",
		TIP_ALT_SUMMON = "L'amène à vous : un alt se connecte là où il s'est déconnecté.",
		ALTS_FORGET = "Clic droit : oublier ce personnage.", ALTS_FORGOT = "%s n'est plus listé dans Mes alts.",
		ALTS_QUESTS = "Quêtes", TIP_ALT_QUESTS = "Les quêtes qu'il a en cours, avec leurs objectifs (quests all).",
		M_QUESTS = "Ses quêtes", Q_TITLE = "Quêtes de %s", Q_WAIT = "Question à %s...", Q_NO_REPLY = "Pas de réponse de %s.", Q_OTHER = "Une liste de quêtes est venue de \"%s\", pas de %s : signalez ce nom.",
		Q_INCOMPLETE = "En cours", Q_COMPLETE = "À rendre", Q_REFRESH = "Redemander",
		Q_HELP = "Les quêtes en cours avec leurs objectifs, puis celles à rendre.",
		ROLES_ON = "Rôles automatiques activés : un membre sans rôle de groupe est inspecté pour connaître sa spécialisation.",
		ROLES_OFF = "Rôles automatiques désactivés.",
		RAIDFRAMES_ON = "Icônes de rôle et menus des bots sur les cadres de raid : activés.",
		RAIDFRAMES_OFF = "Icônes de rôle et menus des bots sur les cadres de raid : désactivés.",
		HELP = "SquidBots Lite : /sbl (afficher/cacher), /sbl show, /sbl hide, /sbl scale 0.5-2, /sbl lang fr|en, /sbl alts, /sbl quests <nom>, /sbl roles on|off, /sbl raidframes on|off, /sbl gm, /sbl reset. Raccourcis : Échap > Raccourcis > SquidBots Lite.",
		SCALE_SET = "Taille de la barre SquidBots Lite : %s (ce personnage).", SCALE_BAD = "Taille de la barre : un nombre de 0,5 à 2, par exemple /sbl scale 1.2",
		LANG_SET = "Langue de SquidBots Lite : français.",
	},
}

local L
-- The small round role icons of the party frames (64 x 64, 19-pixel icons; the same file on 7.3.5).
local ROLE_ICON = "Interface\\LFGFrame\\UI-LFG-ICON-PORTRAITROLES"
local ROLE_COORDS = {
	tank = { 0, 19 / 64, 22 / 64, 41 / 64 },
	heal = { 20 / 64, 39 / 64, 1 / 64, 20 / 64 },
	dps = { 20 / 64, 39 / 64, 22 / 64, 41 / 64 },
}
local FROM_BLIZZARD_ROLE = { TANK = "tank", HEALER = "heal", DAMAGER = "dps" }
-- Captions in a smaller font than the game's small text, and buttons spaced by the widest caption of the
-- current language: English "Dungeon" needs more room than French "Donjon".
local CAPTION_SIZE, CAPTION_GAP, MIN_STEP, FALLBACK_STEP = 8, 3, 37, 52
local BAR_ORDER = { "follow", "stay", "attack", "passive", "dungeon", "summon", "alts", "recruit", "release" }
-- A member without a party role is inspected this long after it shows up, again after PROBE_RETRY.
local PROBE_DELAY, PROBE_RETRY = 3, 30
-- Every co / nc order makes each bot whisper its strategies back ("Combat strategies: ..."): kept out of the
-- chat this long after the addon sent one.
local QUIET_SECONDS = 4
local ICON_ACTIVE, ICON_PASSIVE = "Interface\\Icons\\Ability_Warrior_Charge", "Interface\\Icons\\Spell_Nature_Sleep"
-- The panels: a dark plain background under the dialog border, since the dialog background let the floating
-- names of the world show through.
local PANEL_BACKDROP = { bgFile = "Interface\\Buttons\\WHITE8X8", edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
	tile = false, edgeSize = 24, insets = { left = 6, right = 6, top = 6, bottom = 6 } }
local BINDINGS = { { "follow", "FOLLOW" }, { "stay", "STAY" }, { "attack", "M_ATTACK" }, { "passive", "TOGGLE_PASSIVE" },
	{ "dungeon", "DUNGEON" }, { "summon", "SUMMON" }, { "alts", "ALTS" }, { "release", "RELEASE" } }

local roleByName, firstSeen, probedAt = {}, {}, {}
-- Ranged damage dealers by the specialization an inspect found (orders by role: "Ranged: follow me").
local rangedByName = {}
local RANGED_SPECS = { [62] = true, [63] = true, [64] = true, [102] = true, [253] = true, [254] = true, [258] = true,
	[262] = true, [265] = true, [266] = true, [267] = true }
local state = { order = "follow", passive = false }
local bar, buttons, overlays = nil, {}, {}
local quietUntil = 0

local ui = {
	CARD = { bgFile = "Interface\\Buttons\\WHITE8X8", edgeFile = "Interface\\Buttons\\WHITE8X8", edgeSize = 1,
		insets = { left = 1, right = 1, top = 1, bottom = 1 } },
	CLASS_CIRCLES = "Interface\\TargetingFrame\\UI-Classes-Circles",
	NOBODY = "Interface\\CharacterFrame\\TempPortrait",
}

local function Print(text)
	DEFAULT_CHAT_FRAME:AddMessage("|cff66ccffSquidBots Lite|r: " .. text)
end

local function SetLanguage(lang)
	if lang ~= "fr" and lang ~= "en" then
		lang = (GetLocale() == "frFR") and "fr" or "en"
	end
	SquidBotsLiteDB.lang = lang
	L = STRINGS[lang]
	-- Names shown in the game's Key Bindings window (the bindings themselves are in Bindings.xml).
	BINDING_HEADER_SQUIDBOTSLITE = "SquidBots Lite"
	for _, binding in ipairs(BINDINGS) do
		_G["BINDING_NAME_SQUIDBOTSLITE_" .. string.upper(binding[1])] = L[binding[2]]
	end
end

-- ---------------------------------------------------------------------------
-- Panels' pieces
-- ---------------------------------------------------------------------------
-- A card: dark, a thin border that turns gold under the mouse.
function ui.Card(frame)
	frame:SetBackdrop(ui.CARD)
	frame:SetBackdropColor(0.09, 0.09, 0.11, 0.95)
	frame:SetBackdropBorderColor(1, 1, 1, 0.08)
	frame:EnableMouse(true)
	frame:HookScript("OnEnter", function(self) self:SetBackdropBorderColor(1, 0.82, 0, 0.55) end)
	frame:HookScript("OnLeave", function(self) self:SetBackdropBorderColor(1, 1, 1, 0.08) end)
end

local function ClassColored(text, token)
	local color = token and RAID_CLASS_COLORS and RAID_CLASS_COLORS[token]
	if not color then return text end
	return string.format("|cff%02x%02x%02x%s|r", color.r * 255, color.g * 255, color.b * 255, text)
end

-- Its class icon, from the game's class circles.
function ui.ClassIcon(texture, name)
	local info = SquidBotsLiteDB.altInfo and SquidBotsLiteDB.altInfo[name]
	local coords = info and info.token and CLASS_ICON_TCOORDS and CLASS_ICON_TCOORDS[info.token]
	if coords then
		texture:SetTexture(ui.CLASS_CIRCLES)
		texture:SetTexCoord(coords[1], coords[2], coords[3], coords[4])
	else
		texture:SetTexture(ui.NOBODY)
		texture:SetTexCoord(0, 1, 0, 1)
	end
end

-- A square icon button; tip(self) returns its title and its line.
function ui.IconButton(parent, size, icon, tip, onClick)
	local button = CreateFrame("Button", nil, parent)
	button:SetWidth(size)
	button:SetHeight(size)
	button.icon = button:CreateTexture(nil, "ARTWORK")
	button.icon:SetAllPoints()
	button.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
	button.icon:SetTexture(icon)
	button.border = button:CreateTexture(nil, "BACKGROUND")
	button.border:SetTexture("Interface\\Buttons\\WHITE8X8")
	button.border:SetVertexColor(0, 0, 0, 0.9)
	button.border:SetPoint("TOPLEFT", -1, 1)
	button.border:SetPoint("BOTTOMRIGHT", 1, -1)
	button:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
	button:SetScript("OnMouseDown", function(self) if self:IsEnabled() then self.icon:SetVertexColor(0.6, 0.6, 0.6) end end)
	button:SetScript("OnMouseUp", function(self) self.icon:SetVertexColor(1, 1, 1) end)
	button:SetScript("OnClick", onClick)
	button:SetScript("OnEnter", function(self)
		local title, line = tip(self)
		GameTooltip:SetOwner(self, "ANCHOR_TOP")
		GameTooltip:SetText(title or "", 1, 0.82, 0)
		if line then GameTooltip:AddLine(line, 1, 1, 1, true) end
		GameTooltip:Show()
	end)
	button:SetScript("OnLeave", function() GameTooltip:Hide() end)
	return button
end

function ui.SetIcon(button, icon, enabled)
	button.icon:SetTexture(icon)
	button.icon:SetDesaturated(not enabled)
	if enabled then button:Enable() else button:Disable() end
end

-- The game's red buttons (UIPanelButtonTemplate) made flat: dark, a thin gold border, a gold glow under the mouse.
local FLAT_EDGE = { edgeFile = "Interface\\Buttons\\WHITE8X8", edgeSize = 1 }
function ui.Flatten(button)
	if button.flat then return end
	button.flat = true
	for _, def in ipairs({ { "Normal", 0.13, 0.13, 0.15, 0.95 }, { "Pushed", 0.07, 0.07, 0.08, 0.95 },
		{ "Disabled", 0.09, 0.09, 0.10, 0.8 }, { "Highlight", 1, 0.82, 0, 0.2 } }) do
		button["Set" .. def[1] .. "Texture"](button, "Interface\\Buttons\\WHITE8X8")
		local texture = button["Get" .. def[1] .. "Texture"](button)
		texture:SetTexCoord(0, 1, 0, 1)
		texture:SetVertexColor(def[2], def[3], def[4], def[5])
		texture:ClearAllPoints()
		texture:SetPoint("TOPLEFT", 1, -1)
		texture:SetPoint("BOTTOMRIGHT", -1, 1)
	end
	button:GetHighlightTexture():SetBlendMode("ADD")
	button:SetBackdrop(FLAT_EDGE)
	button:SetBackdropBorderColor(1, 0.82, 0, 0.35)
end

-- A thin gold line across a panel, at y from its top.
function ui.Rule(f, y)
	local rule = f:CreateTexture(nil, "ARTWORK")
	rule:SetTexture("Interface\\Buttons\\WHITE8X8")
	rule:SetVertexColor(1, 0.82, 0, 0.18)
	rule:SetHeight(1)
	rule:SetPoint("TOPLEFT", 16, y)
	rule:SetPoint("TOPRIGHT", -16, y)
	return rule
end

-- ---------------------------------------------------------------------------
-- The group
-- ---------------------------------------------------------------------------
-- The group's member units, the player left out: raid1-40 in a raid, party1-4 otherwise.
local function GroupUnits()
	local units = {}
	if IsInRaid() then
		for i = 1, GetNumGroupMembers() do
			local unit = "raid" .. i
			if UnitExists(unit) and not UnitIsUnit(unit, "player") then table.insert(units, unit) end
		end
	else
		for i = 1, GetNumSubgroupMembers() do table.insert(units, "party" .. i) end
	end
	return units
end

-- A group member's unit id, or nil.
local function UnitOf(name)
	for _, unit in ipairs(GroupUnits()) do
		if UnitName(unit) == name then return unit end
	end
end

-- A member's role: its party role (DemonCore-Playerbots sets one in dungeon finder groups), else the role of the
-- specialization an inspect found.
local function RoleOf(name, unit)
	unit = unit or UnitOf(name)
	local role = unit and FROM_BLIZZARD_ROLE[UnitGroupRolesAssigned(unit) or ""]
	return role or roleByName[name]
end

local function SetRoleIcon(texture, role)
	if not role then
		texture:SetTexture("Interface\\Icons\\INV_Misc_QuestionMark")
		texture:SetTexCoord(0, 1, 0, 1)
		texture:Show()
		return
	end
	local c = ROLE_COORDS[role]
	texture:SetTexture(ROLE_ICON)
	texture:SetTexCoord(c[1], c[2], c[3], c[4])
	texture:Show()
end

-- A co / nc order (alone, or after a role filter): its answers are hidden for a few seconds.
local function NoteQuiet(message)
	local body = message:gsub("^@%a+%s+", "")
	if body:find("^co ") or body:find("^nc ") or body == "co ?" or body == "nc ?" then
		quietUntil = GetTime() + QUIET_SECONDS
	end
end

local function SendGroup(...)
	if not IsInGroup() then
		Print(L.NOT_IN_GROUP)
		return false
	end
	local channel = IsInRaid() and "RAID" or "PARTY"
	for i = 1, select("#", ...) do
		local message = select(i, ...)
		NoteQuiet(message)
		SendChatMessage(message, channel)
	end
	return true
end

-- A whisper's sender or target without a "-Realm" part (character names hold no hyphen),
-- to compare with UnitName and the names the addon keeps.
local function ShortName(name)
	return name and name:match("^([^%-]+)") or name
end

-- The same character: no realm part, any case ("/sbl quests fogh" is Fogh's list).
local function SameName(a, b)
	return a ~= nil and b ~= nil and string.lower(ShortName(a)) == string.lower(ShortName(b))
end

local function Whisper(name, message)
	NoteQuiet(message)
	SendChatMessage(message, "WHISPER", nil, name)
end

-- GM commands (.playerbots ...): typed in /say like any dot command.
local function GmCommand(text)
	SendChatMessage(".playerbots " .. text, "SAY")
end

-- ---------------------------------------------------------------------------
-- Menus
-- ---------------------------------------------------------------------------
-- A menu closes on its own once the mouse has left it this many seconds (the game keeps it open otherwise).
local MENU_HIDE_DELAY = 1
local menuFrame = CreateFrame("Frame", "SquidBotsLiteMenu", UIParent, "UIDropDownMenuTemplate")
local menuWatch = CreateFrame("Frame")
menuWatch:Hide()
menuWatch:SetScript("OnUpdate", function(self)
	local list1, list2 = _G.DropDownList1, _G.DropDownList2
	if not list1 or not list1:IsShown() then self:Hide() return end
	if MouseIsOver(list1) or (list2 and list2:IsShown() and MouseIsOver(list2)) then
		self.left = nil
	else
		self.left = self.left or GetTime()
		if GetTime() - self.left > MENU_HIDE_DELAY then
			CloseDropDownMenus()
			self:Hide()
		end
	end
end)

local function ShowMenu(items)
	-- A second click on the button that opened it closes it.
	if _G.DropDownList1 and DropDownList1:IsShown() and UIDROPDOWNMENU_OPEN_MENU == menuFrame then
		CloseDropDownMenus()
		return
	end
	EasyMenu(items, menuFrame, "cursor", 0, 0, "MENU", MENU_HIDE_DELAY)
	menuWatch.left = nil
	menuWatch:Show()
end

local ShowAlts, ShowQuests

-- Set by the bar and by the menus alike, so the lit button always matches the last order sent.
local Highlight
local function SetOrder(order)
	state.order = order
	Highlight()
end

-- "passive" fights nothing; "assist full" is back to normal (on DemonCore "aggressive" means pulling on its own).
local function SetPassive(on)
	return SendGroup(on and "passive" or "assist full")
end

-- For every group order: a passive bot is woken first, and the Passive button follows.
local function SendOrder(...)
	if state.passive then
		if not SetPassive(false) then return false end
		state.passive = false
		Highlight()
	end
	return SendGroup(...)
end

-- No pulls of their own ("aggressive" off), the normal assist, "avoidaoe" on (always on, said again), and follow.
local function DungeonMode()
	if SendOrder("co -aggressive,+avoidaoe", "assist full", "follow") then SetOrder("dungeon") end
end

-- Orders by role: the server has no role filter, so the addon whispers each member of that role.
local function SendToRole(role, order)
	if not IsInGroup() then Print(L.NOT_IN_GROUP) return end
	for _, unit in ipairs(GroupUnits()) do
		local name = UnitName(unit)
		local memberRole = name and RoleOf(name, unit)
		local ranged = memberRole == "dps" and rangedByName[name]
		if name and (memberRole == role or (role == "ranged" and (ranged or memberRole == "heal"))) then
			Whisper(name, order)
		end
	end
end

local function RecruitMenu()
	local items = {
		{ text = L.RECRUIT, isTitle = true, notCheckable = true },
		{ text = L.ALTS, notCheckable = true, func = function() ShowAlts() end },
		{ text = L.GEAR, notCheckable = true, func = function() SendGroup("autogear") end },
		-- "maintenance": talents, riding and mounts, enchants and gems, bags, and a repair, without leaving the spot.
		{ text = L.SPELLS, notCheckable = true, func = function() SendGroup("maintenance") end },
		-- "sell gray": grey items, to a vendor within 10 yards (stand next to one).
		{ text = L.SELL, notCheckable = true, func = function() SendGroup("sell gray") end },
		-- "quests available": what the nearest quest giver offers each bot ("quest take <name>" takes one).
		{ text = L.QUESTS, notCheckable = true, func = function() SendGroup("quests available") end },
	}
	if SquidBotsLiteDB.gm then
		table.insert(items, { text = L.ACTIVE_BOTS, notCheckable = true, func = function() GmCommand("list") end })
	end
	return items
end

local function ProfileMenu()
	local roleOrders = {
		{ text = L.R_TANK_ATTACK, notCheckable = true, func = function() SendToRole("tank", "attack") end },
		{ text = L.R_DPS_ATTACK, notCheckable = true, func = function() SendToRole("dps", "attack") end },
		{ text = L.R_HEAL_FOLLOW, notCheckable = true, func = function() SendToRole("heal", "follow") end },
		{ text = L.R_HEAL_STAY, notCheckable = true, func = function() SendToRole("heal", "stay") end },
		{ text = L.R_RANGED_FOLLOW, notCheckable = true, func = function() SendToRole("ranged", "follow") end },
	}
	return {
		{ text = L.PROFILES, isTitle = true, notCheckable = true },
		-- "aggressive" pulls what is near, "loot" loots the kills (alts too, by this order), "rest" eats and drinks.
		{ text = L.P_FARM, notCheckable = true, func = function() SendOrder("co +aggressive", "nc +loot,+rest,+follow") end },
		{ text = L.P_STRICT, notCheckable = true, func = function()
			if SendOrder("co -aggressive", "nc +follow", "follow") then SetOrder("follow") end
		end },
		{ text = L.P_DUNGEON, notCheckable = true, func = DungeonMode },
		{ text = L.ROLE_ORDERS, hasArrow = true, notCheckable = true, menuList = roleOrders },
		{ text = L.GEAR, notCheckable = true, func = function() SendGroup("autogear") end },
		{ text = L.SELL, notCheckable = true, func = function() SendGroup("sell gray") end },
	}
end

-- The specializations of the member's class, by index ("talents 1" .. "talents 4").
local function SpecMenu(name)
	local unit = UnitOf(name)
	local classID = unit and select(3, UnitClass(unit))
	if not classID or not GetNumSpecializationsForClassID then return nil end
	local menu = {}
	for i = 1, GetNumSpecializationsForClassID(classID) do
		local _, specName = GetSpecializationInfoForClassID(classID, i)
		if specName then
			table.insert(menu, { text = specName, notCheckable = true, func = function() Whisper(name, "talents " .. i) end })
		end
	end
	return #menu > 0 and menu or nil
end

local function BotMenu(name)
	local role = RoleOf(name)
	local items = {
		{ text = name, isTitle = true, notCheckable = true },
		{ text = L.M_FOLLOW, notCheckable = true, func = function() Whisper(name, "follow") end },
		{ text = L.M_STAY, notCheckable = true, func = function() Whisper(name, "stay") end },
		{ text = L.M_ATTACK, notCheckable = true, func = function() Whisper(name, "attack") end },
		{ text = L.M_SUMMON, notCheckable = true, func = function() Whisper(name, "summon") end },
		{ text = L.M_GEAR, notCheckable = true, func = function() Whisper(name, "autogear") end },
		{ text = L.M_SPELLS, notCheckable = true, func = function() Whisper(name, "maintenance") end },
		{ text = L.M_SELL, notCheckable = true, func = function() Whisper(name, "sell gray") end },
		{ text = L.M_QUESTS, notCheckable = true, func = function() ShowQuests(name) end },
	}
	if UnitIsGroupLeader("player") then
		table.insert(items, { text = L.M_MAKE_LEADER, notCheckable = true, func = function() PromoteToLeader(name) end })
	end
	if role == "tank" then
		-- "player tank": the player holds the aggro, tank bots neither taunt nor take enemies off the group.
		table.insert(items, { text = L.M_PLAYER_TANK, notCheckable = true, func = function() Whisper(name, "player tank") end })
		table.insert(items, { text = L.M_BOT_TANK, notCheckable = true, func = function() Whisper(name, "bot tank") end })
	end
	-- "talents <1-4>": switch to that specialization and fill its talent rows (an order, so alts too).
	local specs = SpecMenu(name)
	if specs then
		table.insert(items, { text = L.M_SPEC, hasArrow = true, notCheckable = true, menuList = specs })
	end
	if SquidBotsLiteDB.gm then
		table.insert(items, { text = L.M_LOGOUT, notCheckable = true, func = function() GmCommand("remove " .. name) end })
	end
	table.insert(items, { text = L.M_KICK, notCheckable = true, func = function() UninviteUnit(name) end })
	return items
end

-- ---------------------------------------------------------------------------
-- The bar
-- ---------------------------------------------------------------------------
Highlight = function()
	for key, button in pairs(buttons) do
		local active = key == state.order or (key == "passive" and state.passive)
		button.ring:SetVertexColor(active and 1 or 0.35, active and 0.82 or 0.35, active and 0 or 0.35)
	end
	local passive = buttons.passive
	if passive then
		passive.icon:SetTexture(state.passive and ICON_PASSIVE or ICON_ACTIVE)
		passive.label = state.passive and "PASSIVE" or "ACTIVE"
		passive.tip = state.passive and "TIP_PASSIVE" or "TIP_ACTIVE"
		passive.caption:SetText(L[passive.label])
	end
end

-- Size and place of the bar, and whether it shows, are kept per character (SquidBotsLiteCharDB).
local SCALE_MIN, SCALE_MAX = 0.5, 2

local function PlaceBar()
	local p = SquidBotsLiteCharDB.point
	bar:ClearAllPoints()
	if p then
		bar:SetPoint(p[1], UIParent, p[2], p[3], p[4])
	else
		bar:SetPoint("BOTTOM", UIParent, "BOTTOM", 0, 150)
	end
end

local function SetBarScale(scale, quiet)
	scale = math.floor(math.max(SCALE_MIN, math.min(SCALE_MAX, scale)) * 100 + 0.5) / 100
	SquidBotsLiteCharDB.scale = scale
	if bar then bar:SetScale(scale) end
	if not quiet then Print(string.format(L.SCALE_SET, scale)) end
end

local function SetBarShown(shown)
	SquidBotsLiteCharDB.hidden = not shown
	if shown then bar:Show() else bar:Hide() end
end

local function BarButton(key, icon, label, tip, onClick)
	local button = CreateFrame("Button", nil, bar)
	button:SetWidth(34)
	button:SetHeight(34)
	local texture = button:CreateTexture(nil, "ARTWORK")
	texture:SetAllPoints()
	texture:SetTexture(icon)
	button.icon = texture
	texture:SetTexCoord(0.07, 0.93, 0.07, 0.93)
	button.ring = button:CreateTexture(nil, "OVERLAY")
	button.ring:SetTexture("Interface\\Buttons\\UI-ActionButton-Border")
	button.ring:SetBlendMode("ADD")
	button.ring:SetWidth(62)
	button.ring:SetHeight(62)
	button.ring:SetPoint("CENTER")
	button.ring:SetAlpha(0.8)
	button.caption = button:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
	local font, _, flags = button.caption:GetFont()
	button.caption:SetFont(font, CAPTION_SIZE, flags)
	button.caption:SetPoint("TOP", button, "BOTTOM", 0, -1)
	button.label, button.tip, button.onClick = label, tip, onClick
	button:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
	button:RegisterForClicks("LeftButtonUp", "RightButtonUp")
	button:RegisterForDrag("LeftButton")
	button:SetScript("OnDragStart", function() if IsShiftKeyDown() then bar:StartMoving() end end)
	button:SetScript("OnDragStop", function()
		bar:StopMovingOrSizing()
		local point, _, relativePoint, x, y = bar:GetPoint()
		SquidBotsLiteCharDB.point = { point, relativePoint, x, y }
	end)
	-- Ctrl + mouse wheel over the bar: bigger or smaller, by steps of 0.05.
	button:EnableMouseWheel(true)
	button:SetScript("OnMouseWheel", function(_, delta)
		if IsControlKeyDown() then SetBarScale((SquidBotsLiteCharDB.scale or 1) + 0.05 * delta, true) end
	end)
	button:SetScript("OnClick", function(self, mouse)
		-- Right click anywhere on the bar: the profiles (farm, strict follow, dungeon...).
		if mouse == "RightButton" then
			ShowMenu(ProfileMenu())
		else
			onClick(self, mouse)
		end
	end)
	button:SetScript("OnEnter", function(self)
		GameTooltip:SetOwner(self, "ANCHOR_TOP")
		GameTooltip:SetText(L[self.label])
		GameTooltip:AddLine(L[self.tip], 1, 1, 1, true)
		GameTooltip:AddLine(L.TIP_PROFILES, 0.6, 0.6, 0.6, true)
		GameTooltip:AddLine(L.TIP_MOVE, 0.6, 0.6, 0.6, true)
		GameTooltip:Show()
	end)
	button:SetScript("OnLeave", function() GameTooltip:Hide() end)
	buttons[key] = button
	return button
end

-- Spaces the buttons evenly, as close as two neighbouring captions allow. "Active" and "Passive" are both
-- counted, so the bar does not move when the button flips.
local function LayoutBar()
	local measure = buttons.follow.caption
	local shown = measure:GetText()
	local function Width(text)
		measure:SetText(text)
		return measure:GetStringWidth() or 0
	end
	local widths = {}
	for i, key in ipairs(BAR_ORDER) do
		widths[i] = key == "passive" and math.max(Width(L.ACTIVE), Width(L.PASSIVE)) or Width(L[buttons[key].label])
	end
	measure:SetText(shown)
	local step = 0
	for i = 1, #widths - 1 do
		step = math.max(step, (widths[i] + widths[i + 1]) / 2)
	end
	step = step > 0 and math.max(MIN_STEP, math.ceil(step) + CAPTION_GAP) or FALLBACK_STEP
	for i, key in ipairs(BAR_ORDER) do
		buttons[key]:ClearAllPoints()
		buttons[key]:SetPoint("TOPLEFT", 3 + (i - 1) * step, 0)
	end
	bar:SetWidth(#BAR_ORDER * step)
end

local function RefreshTexts()
	for _, button in pairs(buttons) do
		button.caption:SetText(L[button.label])
	end
	if buttons.follow then LayoutBar() end
end

-- Group members lying dead (a ghost is already on its way back).
local function DeadMembers()
	local dead = {}
	for _, unit in ipairs(GroupUnits()) do
		if UnitIsDead(unit) and not UnitIsGhost(unit) then
			-- The name only: UnitName also returns the realm.
			local name = UnitName(unit)
			if name then table.insert(dead, name) end
		end
	end
	return dead
end

local function BuildBar()
	bar = CreateFrame("Frame", "SquidBotsLiteBar", UIParent)
	bar:SetWidth(#BAR_ORDER * FALLBACK_STEP)
	bar:SetHeight(48)
	bar:SetMovable(true)
	bar:SetClampedToScreen(true)
	bar:SetScale(SquidBotsLiteCharDB.scale or 1)
	PlaceBar()
	local defs = {
		{ "follow", "Interface\\Icons\\Ability_Hunter_Pathfinding", "FOLLOW", "TIP_FOLLOW", function()
			if SendOrder("follow") then SetOrder("follow") end
		end },
		{ "stay", "Interface\\Icons\\Spell_Nature_TimeStop", "STAY", "TIP_STAY", function()
			if SendOrder("stay") then SetOrder("stay") end
		end },
		{ "attack", "Interface\\Icons\\Ability_DualWield", "ATTACK", "TIP_ATTACK", function()
			if SendOrder("attack") then SetOrder("attack") end
		end },
		{ "passive", ICON_ACTIVE, "ACTIVE", "TIP_ACTIVE", function()
			if SetPassive(not state.passive) then state.passive = not state.passive; Highlight() end
		end },
		{ "dungeon", "Interface\\Icons\\Achievement_Dungeon_ClassicDungeonMaster", "DUNGEON", "TIP_DUNGEON", DungeonMode },
		{ "summon", "Interface\\Icons\\Spell_Arcane_PortalDalaran", "SUMMON", "TIP_SUMMON", function()
			SendGroup("summon")
		end },
		{ "alts", "Interface\\Icons\\INV_Misc_Head_Human_01", "ALTS_BAR", "TIP_ALTS", function()
			ShowAlts()
		end },
		{ "recruit", "Interface\\Icons\\Spell_Frost_SummonWaterElemental", "RECRUIT", "TIP_RECRUIT", function()
			ShowMenu(RecruitMenu())
		end },
		-- On DemonCore "summon" revives a dead bot only when said in party / raid chat (out of combat by default);
		-- whispered, it only tells how. So Revive is the group's summon.
		{ "release", "Interface\\Icons\\Spell_Holy_Resurrection", "RELEASE", "TIP_RELEASE", function()
			SendGroup("summon")
		end },
	}
	for _, def in ipairs(defs) do
		BarButton(def[1], def[2], def[3], def[4], def[5])
	end
	buttons.release:Hide()
	RefreshTexts()
	Highlight()
	if SquidBotsLiteCharDB.hidden then
		bar:Hide()
	end
end

local function RefreshRelease()
	if #DeadMembers() > 0 then buttons.release:Show() else buttons.release:Hide() end
end

-- ---------------------------------------------------------------------------
-- Party frame overlays: role icon, click for the bot's menu
-- ---------------------------------------------------------------------------
local function OverlayClick(self)
	local name = self.unit and UnitName(self.unit)
	if name then ShowMenu(BotMenu(name)) end
end

local function OverlayTooltip(self)
	local name = self.unit and UnitName(self.unit)
	if not name then return end
	GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
	GameTooltip:SetText(name)
	local role = RoleOf(name, self.unit)
	if role then
		local keys = { tank = "ROLE_TANK", heal = "ROLE_HEAL", dps = "ROLE_DPS" }
		GameTooltip:AddLine(L[keys[role]], 1, 1, 1)
	end
	GameTooltip:AddLine(L.TIP_BOT_ICON, 0.6, 0.6, 0.6)
	GameTooltip:Show()
end

local function BuildOverlays()
	for i = 1, 4 do
		local frame = _G["PartyMemberFrame" .. i]
		if frame then
			local overlay = CreateFrame("Button", nil, frame)
			overlay:SetWidth(18)
			overlay:SetHeight(18)
			overlay:SetPoint("TOPLEFT", frame, "TOPLEFT", -4, 4)
			overlay:SetFrameLevel(frame:GetFrameLevel() + 5)
			-- Ours shows the role and opens the menu: the frame's own role icon is hidden under it.
			local nativeRole = _G["PartyMemberFrame" .. i .. "RoleIcon"]
			if nativeRole then nativeRole:SetAlpha(0) end
			overlay.icon = overlay:CreateTexture(nil, "OVERLAY")
			overlay.icon:SetAllPoints()
			overlay.unit = "party" .. i
			overlay:SetScript("OnClick", OverlayClick)
			overlay:SetScript("OnEnter", OverlayTooltip)
			overlay:SetScript("OnLeave", function() GameTooltip:Hide() end)
			overlays[i] = overlay
		end
	end
end

local function RefreshOverlays()
	for _, overlay in pairs(overlays) do
		local name = UnitName(overlay.unit)
		if name then
			overlay:Show()
			SetRoleIcon(overlay.icon, RoleOf(name, overlay.unit))
		else
			overlay:Hide()
		end
	end
end

-- Raid-style frames (CompactPartyFrame / CompactRaidFrame, "Use Raid-Style Party Frames"): the same role icon
-- and bot menu. Their frames are made on demand and change units, so they are looked for every few seconds and
-- the unit is read from the frame at each refresh. Our icons belong to UIParent and are only anchored to those
-- secure frames, which keeps them out of combat locks.
local compact = { overlays = {}, scanAt = 0, groupSize = -1, SCAN = 10, RESCAN = 0.5 }

function compact.Unit(frame)
	local unit = frame.displayedUnit or frame.unit
	if type(unit) ~= "string" and frame.GetAttribute then unit = frame:GetAttribute("unit") end
	return type(unit) == "string" and unit or nil
end

-- A CompactUnitFrame of the group: a unit and a "healthBar" (the old party frames have "healthbar"). A
-- nameplate never matches: only party and raid units are ours.
function compact.IsFrame(frame)
	local unit = frame.healthBar and compact.Unit(frame)
	if not unit or not (unit == "player" or string.find(unit, "^party%d") or string.find(unit, "^raid%d")) then
		return nil
	end
	return unit
end

function compact.Overlay(frame)
	local overlay = CreateFrame("Button", nil, UIParent)
	overlay:SetWidth(14)
	overlay:SetHeight(14)
	overlay:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -2, -2)
	overlay.icon = overlay:CreateTexture(nil, "OVERLAY")
	overlay.icon:SetAllPoints()
	overlay:SetScript("OnClick", OverlayClick)
	overlay:SetScript("OnEnter", OverlayTooltip)
	overlay:SetScript("OnLeave", function() GameTooltip:Hide() end)
	overlay.frame = frame
	compact.overlays[frame] = overlay
	return overlay
end

function compact.Scan()
	local frame = EnumerateFrames()
	while frame do
		if not compact.overlays[frame] and compact.IsFrame(frame) then compact.Overlay(frame) end
		frame = EnumerateFrames(frame)
	end
end

function compact.Refresh()
	if SquidBotsLiteDB.raidFrames == false then
		for _, overlay in pairs(compact.overlays) do overlay:Hide() end
		return
	end
	local now = GetTime()
	local size = GetNumGroupMembers()
	if size ~= compact.groupSize then
		compact.groupSize = size
		compact.scanAt = math.min(compact.scanAt, now + compact.RESCAN)
	end
	if size > 0 and now >= compact.scanAt then
		compact.scanAt = now + compact.SCAN
		if _G.CompactRaidFrameContainer or _G.CompactPartyFrame then compact.Scan() end
	end
	for frame, overlay in pairs(compact.overlays) do
		local unit = frame:IsVisible() and compact.Unit(frame)
		local name = unit and not UnitIsUnit(unit, "player") and UnitName(unit)
		if name then
			overlay.unit = unit
			-- A frame can report a strata SetFrameStrata does not take back ("UNKNOWN"): keep the one we have.
			local strata = frame:GetFrameStrata()
			if strata and strata ~= "UNKNOWN" then overlay:SetFrameStrata(strata) end
			overlay:SetFrameLevel(frame:GetFrameLevel() + 10)
			if frame.roleIcon then frame.roleIcon:SetAlpha(0) end
			SetRoleIcon(overlay.icon, RoleOf(name, unit))
			overlay:Show()
		else
			overlay.unit = nil
			overlay:Hide()
		end
	end
end

-- ---------------------------------------------------------------------------
-- My alts: the player's other characters, logged in as bots with the GM ".playerbots add / remove"
-- ---------------------------------------------------------------------------
-- The server cannot list an account's characters to a player: every character that logs in with the addon
-- notes itself (name, level, class, specialization) in the account-wide SquidBotsLiteDB.altInfo, and My alts
-- lists them. Which ones are logged in as bots comes from ".playerbots list".
local ALT_ROWS = 8
local ALT_ROW_H = 50
local ALT_TOP = 74
local ROSTER_WAIT, AFTER_TOGGLE, INVITE_DELAY = 5, 3, 1.5   -- seconds
local altsFrame, altRows, roster = nil, {}, {}
local onlineBots = {}
local rosterAskedAt, refreshAt, readingList, hideListUntil
local pendingInvites = {}

local function NoteSelf()
	local name = UnitName("player")
	local level = UnitLevel("player")
	if not name or not level or level <= 0 then return end
	local class, token = UnitClass("player")
	SquidBotsLiteDB.altInfo = SquidBotsLiteDB.altInfo or {}
	local info = SquidBotsLiteDB.altInfo[name] or {}
	info.level, info.class, info.token = level, class, token
	local spec = GetSpecialization and GetSpecialization()
	info.spec = spec and select(2, GetSpecializationInfo(spec)) or nil
	SquidBotsLiteDB.altInfo[name] = info
end

-- An alt seen in the group keeps its level up to date.
local function NoteGroupAlts()
	for _, unit in ipairs(GroupUnits()) do
		local name = UnitName(unit)
		local info = name and SquidBotsLiteDB.altInfo and SquidBotsLiteDB.altInfo[name]
		local level = UnitLevel(unit)
		if info and level and level > 0 then info.level = level end
	end
end

local function BuildRoster()
	local list, me = {}, UnitName("player")
	for name in pairs(SquidBotsLiteDB.altInfo or {}) do
		-- A party survives a server restart: a bot that is still in the group but disconnected is offline.
		local unit = UnitOf(name)
		local online = onlineBots[name] or (unit ~= nil and UnitIsConnected(unit) and true) or false
		if name ~= me then table.insert(list, { name = name, online = online }) end
	end
	table.sort(list, function(a, b)
		if a.online ~= b.online then return a.online end
		return a.name < b.name
	end)
	return list
end

local function AskRoster()
	rosterAskedAt, refreshAt = GetTime(), nil
	altsFrame.status:SetText(L.ALTS_WAIT)
	GmCommand("list")
end

local function AltLine(name)
	local info = SquidBotsLiteDB.altInfo and SquidBotsLiteDB.altInfo[name]
	if not info or not info.level then return "|cff808080" .. L.ALTS_UNKNOWN .. "|r" end
	local line = string.format(L.ALTS_LEVEL, info.level, ClassColored(info.class or "", info.token))
	if info.spec then line = line .. " |cffaaaaaa- " .. info.spec .. "|r" end
	return line
end

local function RefreshAlts()
	if not altsFrame then return end
	NoteGroupAlts()
	roster = BuildRoster()
	altsFrame:SetHeight(ALT_TOP + math.max(1, math.min(#roster, ALT_ROWS)) * ALT_ROW_H + 44)
	FauxScrollFrame_Update(altsFrame.scroll, #roster, ALT_ROWS, ALT_ROW_H)
	local offset = FauxScrollFrame_GetOffset(altsFrame.scroll)
	local online = 0
	for _, alt in ipairs(roster) do if alt.online then online = online + 1 end end
	altsFrame.count:SetText(#roster > 0 and string.format(L.ALTS_COUNT, online, #roster) or "")
	for i, row in ipairs(altRows) do
		local alt = roster[i + offset]
		row.alt = alt
		if alt then
			local info = SquidBotsLiteDB.altInfo[alt.name] or {}
			row.name:SetText(alt.online and ClassColored(alt.name, info.token) or ("|cff9d9d9d" .. alt.name .. "|r"))
			row.info:SetText(AltLine(alt.name))
			ui.ClassIcon(row.icon, alt.name)
			row.icon:SetDesaturated(not alt.online)
			row.icon:SetAlpha(alt.online and 1 or 0.55)
			row.dot:SetTexture(alt.online and "Interface\\FriendsFrame\\StatusIcon-Online" or "Interface\\FriendsFrame\\StatusIcon-Offline")
			row:SetBackdropColor(alt.online and 0.10 or 0.07, alt.online and 0.10 or 0.07, alt.online and 0.12 or 0.08, 0.95)
			ui.SetIcon(row.toggle, alt.online and "Interface\\Icons\\Spell_ChargeNegative" or "Interface\\Icons\\Spell_ChargePositive", true)
			if alt.online then row.summon:Show() row.quests:Show() else row.summon:Hide() row.quests:Hide() end
			row:Show()
		else
			row:Hide()
		end
	end
	if #roster == 0 then
		altsFrame.status:SetText(L.ALTS_EMPTY)
	elseif not rosterAskedAt then
		altsFrame.status:SetText("")
	end
end

local function AltsTexts()
	if not altsFrame then return end
	altsFrame.title:SetText("SquidBots Lite - " .. L.ALTS)
	altsFrame.teams:SetText(L.TEAMS)
	altsFrame.help:SetText(L.ALTS_HELP)
end

-- Teams: named lists of characters kept account-wide (SquidBotsLiteDB.teams), logged in one a second.
local loginQueue, loginNext = {}, 0

local function QueueBots(command, names)
	for _, name in ipairs(names) do table.insert(loginQueue, command .. " " .. name) end
end

local function TeamNames(name)
	local names = {}
	for _, n in ipairs((SquidBotsLiteDB.teams or {})[name] or {}) do table.insert(names, n) end
	return names
end

local function GroupNames()
	local names = {}
	for _, unit in ipairs(GroupUnits()) do
		local n = UnitName(unit)
		if n and n ~= UNKNOWNOBJECT then table.insert(names, n) end
	end
	return names
end

local function OnlineAltNames()
	local names = {}
	for _, alt in ipairs(roster) do if alt.online then table.insert(names, alt.name) end end
	return names
end

local pendingTeam
StaticPopupDialogs.SQUIDBOTSLITE_TEAM = {
	text = "%s", button1 = OKAY, button2 = CANCEL, hasEditBox = 1, maxLetters = 24, timeout = 0, whileDead = 1,
	hideOnEscape = 1,
	OnAccept = function(self)
		local box = _G[self:GetName() .. "EditBox"]
		local name = strtrim(box:GetText() or "")
		if name == "" or not pendingTeam then return end
		SquidBotsLiteDB.teams = SquidBotsLiteDB.teams or {}
		SquidBotsLiteDB.teams[name] = pendingTeam
		Print(string.format(L.T_SAVED, name, #pendingTeam))
		pendingTeam = nil
	end,
	EditBoxOnEnterPressed = function(self)
		StaticPopupDialogs.SQUIDBOTSLITE_TEAM.OnAccept(self:GetParent())
		self:GetParent():Hide()
	end,
	EditBoxOnEscapePressed = function(self) self:GetParent():Hide() end,
}

local function SaveTeam(names)
	if #names == 0 then Print(L.T_EMPTY) return end
	pendingTeam = names
	StaticPopupDialogs.SQUIDBOTSLITE_TEAM.text = L.T_NAME
	StaticPopup_Show("SQUIDBOTSLITE_TEAM")
end

local function TeamsMenu()
	local items = { { text = L.TEAMS, isTitle = true, notCheckable = true } }
	local names = {}
	for name in pairs(SquidBotsLiteDB.teams or {}) do table.insert(names, name) end
	table.sort(names)
	if #names == 0 then table.insert(items, { text = L.T_NONE, notCheckable = true, disabled = true }) end
	for _, name in ipairs(names) do
		local members = TeamNames(name)
		table.insert(items, { text = name, hasArrow = true, notCheckable = true, menuList = {
			{ text = table.concat(members, ", "), notCheckable = true, disabled = true },
			{ text = string.format(L.T_LOGIN, #members), notCheckable = true, func = function()
				QueueBots("add", members)
				Print(string.format(L.T_STARTED, name, #members))
			end },
			{ text = L.T_LOGOUT, notCheckable = true, func = function() QueueBots("remove", members) end },
			{ text = L.T_DELETE, notCheckable = true, func = function() SquidBotsLiteDB.teams[name] = nil end },
		} })
	end
	table.insert(items, { text = L.T_SAVE_GROUP, notCheckable = true, func = function() SaveTeam(GroupNames()) end })
	table.insert(items, { text = L.T_SAVE_ONLINE, notCheckable = true, func = function() SaveTeam(OnlineAltNames()) end })
	ShowMenu(items)
end

-- A bot logged in from here joins the group: SkyFire-playerbots bots accept an invite on their own. A sixth
-- member needs a raid, which only the leader can make.
local function InviteBot(name)
	if UnitOf(name) then return end
	if IsInGroup() and not IsInRaid() and GetNumGroupMembers() >= 5 then
		if not UnitIsGroupLeader("player") then return end
		ConvertToRaid()
	end
	InviteUnit(name)
end

local function TickLogins()
	local now = GetTime()
	for name, at in pairs(pendingInvites) do
		if now >= at then
			pendingInvites[name] = nil
			InviteBot(name)
		end
	end
	if #loginQueue == 0 or now < loginNext then return end
	loginNext = now + 1
	GmCommand(table.remove(loginQueue, 1))
	if #loginQueue == 0 and altsFrame and altsFrame:IsShown() then refreshAt = now + AFTER_TOGGLE end
end

local function BuildAlts()
	altsFrame = CreateFrame("Frame", "SquidBotsLiteAlts", UIParent)
	altsFrame:SetWidth(470)
	altsFrame:SetHeight(ALT_TOP + ALT_ROWS * ALT_ROW_H + 44)
	altsFrame:SetPoint("CENTER")
	altsFrame:SetBackdrop(PANEL_BACKDROP)
	altsFrame:SetBackdropColor(0.04, 0.04, 0.05, 0.96)
	altsFrame:SetFrameStrata("DIALOG")
	altsFrame:SetMovable(true)
	altsFrame:SetToplevel(true)
	altsFrame:SetScript("OnMouseDown", function() CloseDropDownMenus() end)
	altsFrame:EnableMouse(true)
	altsFrame:SetClampedToScreen(true)
	altsFrame:RegisterForDrag("LeftButton")
	altsFrame:SetScript("OnDragStart", altsFrame.StartMoving)
	altsFrame:SetScript("OnDragStop", altsFrame.StopMovingOrSizing)
	-- Escape closes it, as any game window.
	table.insert(UISpecialFrames, "SquidBotsLiteAlts")
	altsFrame.title = altsFrame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	altsFrame.title:SetPoint("TOP", 0, -14)
	local close = CreateFrame("Button", nil, altsFrame, "UIPanelCloseButton")
	close:SetPoint("TOPRIGHT", -4, -4)
	local teams = CreateFrame("Button", nil, altsFrame, "UIPanelButtonTemplate")
	teams:SetWidth(92)
	teams:SetHeight(22)
	teams:SetPoint("TOPLEFT", 16, -36)
	teams:SetScript("OnClick", TeamsMenu)
	ui.Flatten(teams)
	altsFrame.teams = teams
	altsFrame.refresh = ui.IconButton(altsFrame, 22, "Interface\\Buttons\\UI-RefreshButton",
		function() return L.ALTS_REFRESH, L.TIP_ALTS_REFRESH end, function() AskRoster() end)
	altsFrame.refresh.icon:SetTexCoord(0, 1, 0, 1)
	altsFrame.refresh:SetPoint("TOPRIGHT", -18, -36)
	altsFrame.count = altsFrame:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
	altsFrame.count:SetPoint("RIGHT", altsFrame.refresh, "LEFT", -8, 0)
	ui.Rule(altsFrame, -66)
	altsFrame.status = altsFrame:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
	altsFrame.status:SetPoint("TOPLEFT", 20, -ALT_TOP - 8)
	altsFrame.status:SetWidth(420)
	altsFrame.status:SetJustifyH("LEFT")
	for i = 1, ALT_ROWS do
		local row = CreateFrame("Frame", nil, altsFrame)
		row:SetWidth(414)
		row:SetHeight(ALT_ROW_H - 4)
		row:SetPoint("TOPLEFT", 16, -ALT_TOP - (i - 1) * ALT_ROW_H)
		-- Its class icon, with the friends list's dot: green logged in, grey offline.
		row.icon = row:CreateTexture(nil, "ARTWORK")
		row.icon:SetWidth(34)
		row.icon:SetHeight(34)
		row.icon:SetPoint("LEFT", 6, 0)
		row.dot = row:CreateTexture(nil, "OVERLAY")
		row.dot:SetWidth(14)
		row.dot:SetHeight(14)
		row.dot:SetPoint("BOTTOMRIGHT", row.icon, "BOTTOMRIGHT", 4, -3)
		row.name = row:CreateFontString(nil, "OVERLAY", "GameFontNormal")
		row.name:SetPoint("TOPLEFT", 50, -8)
		row.name:SetWidth(250)
		row.name:SetJustifyH("LEFT")
		row.info = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
		row.info:SetPoint("TOPLEFT", 50, -25)
		row.info:SetWidth(260)
		row.info:SetHeight(12)
		row.info:SetJustifyH("LEFT")
		-- On the right, from the edge: log in / out, then Summon while it is logged in.
		row.toggle = ui.IconButton(row, 26, "Interface\\Icons\\Spell_ChargePositive", function(self)
			local alt = self:GetParent().alt
			if alt and alt.online then return L.ALTS_REMOVE, L.TIP_ALT_REMOVE end
			return L.ALTS_ADD, L.TIP_ALT_ADD
		end, function(self)
			local alt = self:GetParent().alt
			if not alt then return end
			GmCommand((alt.online and "remove " or "add ") .. alt.name)
			ui.SetIcon(self, self.icon:GetTexture(), false)
			refreshAt = GetTime() + AFTER_TOGGLE
		end)
		row.toggle:SetPoint("RIGHT", -10, 0)
		-- An alt logs in where it logged out: Summon brings it over.
		row.summon = ui.IconButton(row, 26, "Interface\\Icons\\Spell_Shadow_Twilight",
			function() return L.ALTS_SUMMON, L.TIP_ALT_SUMMON end, function(self)
			local alt = self:GetParent().alt
			if alt then Whisper(alt.name, "summon") end
		end)
		row.summon:SetPoint("RIGHT", row.toggle, "LEFT", -6, 0)
		row.quests = ui.IconButton(row, 26, "Interface\\Icons\\INV_Misc_Book_08",
			function() return L.ALTS_QUESTS, L.TIP_ALT_QUESTS end, function(self)
			local alt = self:GetParent().alt
			if alt then ShowQuests(alt.name) end
		end)
		row.quests:SetPoint("RIGHT", row.summon, "LEFT", -6, 0)
		row:EnableMouse(true)
		row:SetScript("OnEnter", function(self)
			local alt = self.alt
			if not alt then return end
			GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
			GameTooltip:SetText(alt.name)
			GameTooltip:AddLine(AltLine(alt.name), 1, 1, 1)
			GameTooltip:AddLine(L.ALTS_FORGET, 0.6, 0.6, 0.6, true)
			GameTooltip:Show()
		end)
		row:SetScript("OnLeave", function() GameTooltip:Hide() end)
		-- Right click: a deleted or renamed character goes off the list.
		row:SetScript("OnMouseUp", function(self, mouse)
			if mouse == "RightButton" and self.alt then
				SquidBotsLiteDB.altInfo[self.alt.name] = nil
				Print(string.format(L.ALTS_FORGOT, self.alt.name))
				RefreshAlts()
			end
		end)
		-- After its own scripts: the card's border lights up under the mouse.
		ui.Card(row)
		row:Hide()
		altRows[i] = row
	end
	altsFrame.scroll = CreateFrame("ScrollFrame", "SquidBotsLiteAltsScroll", altsFrame, "FauxScrollFrameTemplate")
	altsFrame.scroll:SetPoint("TOPLEFT", 16, -ALT_TOP)
	altsFrame.scroll:SetPoint("BOTTOMRIGHT", -36, 44)
	altsFrame.scroll:SetScript("OnVerticalScroll", function(self, offset)
		FauxScrollFrame_OnVerticalScroll(self, offset, ALT_ROW_H, RefreshAlts)
	end)
	-- Under the rows, so that their buttons still take the clicks; the rows pass the wheel on to it.
	altsFrame.scroll:SetFrameLevel(altsFrame:GetFrameLevel())
	for _, row in ipairs(altRows) do
		row:EnableMouseWheel(true)
		row:SetScript("OnMouseWheel", function(_, delta)
			local scrollBar = _G.SquidBotsLiteAltsScrollScrollBar
			scrollBar:SetValue(scrollBar:GetValue() - delta * ALT_ROW_H)
		end)
	end
	altsFrame.footer = altsFrame:CreateTexture(nil, "ARTWORK")
	altsFrame.footer:SetTexture("Interface\\Buttons\\WHITE8X8")
	altsFrame.footer:SetVertexColor(1, 0.82, 0, 0.18)
	altsFrame.footer:SetHeight(1)
	altsFrame.footer:SetPoint("BOTTOMLEFT", 16, 34)
	altsFrame.footer:SetPoint("BOTTOMRIGHT", -16, 34)
	altsFrame.help = altsFrame:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
	altsFrame.help:SetPoint("BOTTOMLEFT", 18, 16)
	altsFrame.help:SetWidth(430)
	altsFrame.help:SetJustifyH("LEFT")
	altsFrame:Hide()
	AltsTexts()
end

ShowAlts = function()
	altsFrame:Show()
	RefreshAlts()
	AskRoster()
end

-- Called with the other refreshes: a new list after a log in / out, a word if the server stays silent.
local function TickAlts()
	TickLogins()
	local now = GetTime()
	if readingList and now > readingList then
		readingList = nil
		RefreshAlts()
	end
	if not altsFrame:IsShown() then return end
	if refreshAt and now >= refreshAt then
		AskRoster()
	elseif rosterAskedAt and now - rosterAskedAt > ROSTER_WAIT then
		rosterAskedAt = nil
		altsFrame.status:SetText(L.ALTS_NO_REPLY)
	end
end

-- The server's answers to ".playerbots list / add / remove". Only a list the panel asked for is kept out of
-- the chat; one typed by hand shows.
local function OnSystem(message)
	-- ".playerbots add": "Adding bot 'X'..." at once, "Bot 'X' logged in." once in the world; refused when it is
	-- one already ("'X' is already a bot.") or a player plays it ("'X' is already online.").
	local already = message:match("^'(.-)' is already a bot%.$")
	if already then
		onlineBots[already] = true
		RefreshAlts()
		return
	end
	local added = message:match("^Bot '(.-)' logged in%.$")
	if added then
		onlineBots[added] = true
		pendingInvites[added] = GetTime() + INVITE_DELAY
		RefreshAlts()
		return
	end
	-- ".playerbots remove": "Logging out bot 'X'."; a stale button corrects itself on "'X' is not a bot."
	local removed = message:match("^Logging out bot '(.-)'%.$") or message:match("^'(.-)' is not a bot%.$")
	if removed then
		onlineBots[removed] = nil
		RefreshAlts()
		return
	end
	-- ".playerbots list": "N bot(s):" then "  Name - state" lines, one per bot; "No bots." when none.
	if message:find("^%d+ bot%(s%):$") or message == "No bots." then
		onlineBots, readingList = {}, GetTime() + 1
		if rosterAskedAt then
			rosterAskedAt, hideListUntil = nil, GetTime() + 1
			if altsFrame then altsFrame.status:SetText("") end
		end
		return
	end
	local name = readingList and message:match("^  (%S+) %- .+$")
	if name then
		-- "logging out" is on its way out
		if not message:find(" %- logging out$") then onlineBots[name] = true end
	end
end

ChatFrame_AddMessageEventFilter("CHAT_MSG_SYSTEM", function(_, _, message)
	if hideListUntil and GetTime() < hideListUntil
		and (message:find("^%d+ bot%(s%):$") or message == "No bots." or message:find("^  %S+ %- .+$")) then
		return true
	end
end)

-- ---------------------------------------------------------------------------
-- Quests: a bot's quest log ("quests all"), whispered back line by line
-- ---------------------------------------------------------------------------
-- The answer: "--- Incompleted quests ---", a line per quest ("[Title]", objectives after " - "),
-- "--- Completed quests ---", the same, then "Quests: n / 25 (in progress: x, completed: y)". Kept out of the
-- chat and shown in the panel while it is awaited; "Quests:" ends it.
local QUEST_WAIT = 6   -- seconds without a line before giving up
local questFrame
local questAsk = {}    -- name, lastLine (GetTime), done

-- Only the list's own lines: its headers, lines that start with a quest title in brackets, the total.
local function IsQuestAnswer(message)
	return message:find("^%-%-%- .* %-%-%-$") ~= nil or message:find("^%[.-%]") ~= nil
		or message:find("^Quests: %d+ / %d+") ~= nil
end

local function IsQuestListStart(message)
	return message:find("^%-%-%- .* %-%-%-$") ~= nil
end

local function AwaitsQuests(sender)
	return SameName(questAsk.name, sender) and not questAsk.done and GetTime() - (questAsk.lastLine or 0) < QUEST_WAIT
end

local function BuildQuests()
	questFrame = CreateFrame("Frame", "SquidBotsLiteQuests", UIParent)
	questFrame:SetWidth(460)
	questFrame:SetHeight(380)
	questFrame:SetPoint("CENTER", 40, 0)
	questFrame:SetBackdrop(PANEL_BACKDROP)
	questFrame:SetBackdropColor(0.04, 0.04, 0.05, 0.96)
	questFrame:SetFrameStrata("DIALOG")
	questFrame:SetToplevel(true)
	questFrame:SetMovable(true)
	questFrame:EnableMouse(true)
	questFrame:SetClampedToScreen(true)
	questFrame:RegisterForDrag("LeftButton")
	questFrame:SetScript("OnDragStart", questFrame.StartMoving)
	questFrame:SetScript("OnDragStop", questFrame.StopMovingOrSizing)
	table.insert(UISpecialFrames, "SquidBotsLiteQuests")
	questFrame.title = questFrame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	questFrame.title:SetPoint("TOP", 0, -14)
	local close = CreateFrame("Button", nil, questFrame, "UIPanelCloseButton")
	close:SetPoint("TOPRIGHT", -4, -4)
	questFrame.refresh = ui.IconButton(questFrame, 22, "Interface\\Buttons\\UI-RefreshButton",
		function() return L.Q_REFRESH end, function() if questAsk.name then ShowQuests(questAsk.name) end end)
	questFrame.refresh.icon:SetTexCoord(0, 1, 0, 1)
	questFrame.refresh:SetPoint("TOPLEFT", 18, -12)
	ui.Rule(questFrame, -40)
	-- A message frame: quest links in it show their tooltip and can be shift-clicked into the chat.
	local log = CreateFrame("ScrollingMessageFrame", nil, questFrame)
	log:SetPoint("TOPLEFT", 20, -48)
	log:SetPoint("BOTTOMRIGHT", -20, 40)
	log:SetFontObject(GameFontHighlightSmall)
	log:SetJustifyH("LEFT")
	log:SetFading(false)
	log:SetMaxLines(200)
	log:SetInsertMode("BOTTOM")
	if log.SetIndentedWordWrap then log:SetIndentedWordWrap(true) end
	if log.SetHyperlinksEnabled then log:SetHyperlinksEnabled(true) end
	log:EnableMouseWheel(true)
	log:SetScript("OnMouseWheel", function(self, delta)
		if delta > 0 then self:ScrollUp() else self:ScrollDown() end
	end)
	log:SetScript("OnHyperlinkEnter", function(self, link)
		GameTooltip:SetOwner(self, "ANCHOR_CURSOR")
		GameTooltip:SetHyperlink(link)
		GameTooltip:Show()
	end)
	log:SetScript("OnHyperlinkLeave", function() GameTooltip:Hide() end)
	log:SetScript("OnHyperlinkClick", function(_, link, text, button)
		ChatFrame_OnHyperlinkShow(DEFAULT_CHAT_FRAME, link, text, button)
	end)
	questFrame.log = log
	questFrame.help = questFrame:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
	questFrame.help:SetPoint("BOTTOMLEFT", 20, 16)
	questFrame.help:SetWidth(420)
	questFrame.help:SetJustifyH("LEFT")
	questFrame:Hide()
end

local function OpenQuestWindow(name)
	questFrame.title:SetText(string.format(L.Q_TITLE, name))
	questFrame.help:SetText(L.Q_HELP)
	questFrame.log:Clear()
	questFrame:Show()
end

-- Asked from the addon (button, menu, /sbl quests): the question and the answer stay out of the chat.
ShowQuests = function(name)
	name = ShortName(name)
	if not name or name == "" then return end
	questAsk = { name = name, lastLine = GetTime() }
	OpenQuestWindow(name)
	questFrame.log:AddMessage(string.format(L.Q_WAIT, name), 0.6, 0.6, 0.6)
	SendChatMessage("quests all", "WHISPER", nil, name)
end

-- A list the player asked for by hand ("quests all" typed in chat, to one bot or the group): shown
-- in the window too, and left in the chat. One bot at a time: another's list stays in the chat.
local function CaptureTypedList(sender)
	if questAsk.name and not SameName(questAsk.name, sender) and AwaitsQuests(questAsk.name) then return false end
	questAsk = { name = sender, lastLine = GetTime(), typed = true }
	OpenQuestWindow(sender)
	return true
end

local QUEST_HEADERS = { ["--- Incompleted quests ---"] = "Q_INCOMPLETE", ["--- Completed quests ---"] = "Q_COMPLETE" }

local function OnQuestLine(message)
	local log = questFrame.log
	if not questAsk.started then
		questAsk.started = true
		log:Clear()
	end
	questAsk.lastLine = GetTime()
	local header = QUEST_HEADERS[message]
	if header then
		log:AddMessage(" ")
		log:AddMessage(L[header], 1, 0.82, 0)
	elseif message:find("^Quests: %d+ / %d+") then
		log:AddMessage(" ")
		log:AddMessage(message, 0.6, 0.6, 0.6)
		questAsk.done = true
	else
		log:AddMessage(message, 1, 1, 1)
	end
end

local function TickQuests()
	if questAsk.name and not questAsk.done and not questAsk.started and GetTime() - questAsk.lastLine >= QUEST_WAIT then
		questAsk.done = true
		questFrame.log:Clear()
		questFrame.log:AddMessage(string.format(L.Q_NO_REPLY, questAsk.name), 1, 0.5, 0.3)
		-- The answer may have come under a name the addon did not recognise: say which.
		if questAsk.otherSender then
			questFrame.log:AddMessage(string.format(L.Q_OTHER, questAsk.otherSender, questAsk.name), 1, 0.5, 0.3)
		end
	end
end

ChatFrame_AddMessageEventFilter("CHAT_MSG_WHISPER", function(_, _, message, sender)
	if questAsk.name and not questAsk.typed and SameName(questAsk.name, sender) and IsQuestAnswer(message)
		and GetTime() - (questAsk.lastLine or 0) < QUEST_WAIT + 1 then
		return true
	end
end)
ChatFrame_AddMessageEventFilter("CHAT_MSG_WHISPER_INFORM", function(_, _, message, target)
	if message == "quests all" and not questAsk.typed and SameName(questAsk.name, target)
		and GetTime() - (questAsk.lastLine or 0) < 2 then
		return true
	end
end)

local function OnQuestWhisper(message, sender, rawSender)
	if not IsQuestAnswer(message) then return false end
	if questAsk.name and not questAsk.typed and not SameName(questAsk.name, sender) then
		questAsk.otherSender = rawSender or sender
	end
	if AwaitsQuests(sender) then
		OnQuestLine(message)
		return true
	end
	-- Nothing awaited from this bot: a list that starts now was typed by hand (a finished one
	-- that starts again, too).
	if IsQuestListStart(message) and CaptureTypedList(sender) then
		OnQuestLine(message)
		return true
	end
	return false
end

-- ---------------------------------------------------------------------------
-- Chat: bots' strategy answers
-- ---------------------------------------------------------------------------
-- A bot answers every co / nc order with its strategies ("Combat strategies: ...", "Non-combat strategies
-- changed (+loot). ...").
local function IsStrategyAnswer(message)
	return message:find("^Combat strategies") ~= nil or message:find("^Non%-combat strategies") ~= nil
end

-- A member without a party role is inspected: its specialization gives the role (the server answers an inspect
-- of a bot as of anyone). Never while the player's own inspect window is open. /sbl roles off stops it.
local inspecting

local function ProbeRoles()
	if SquidBotsLiteDB.autoRoles == false or inspecting then return end
	if _G.InspectFrame and InspectFrame:IsShown() then return end
	local now = GetTime()
	for _, unit in ipairs(GroupUnits()) do
		local name = UnitName(unit)
		if name and name ~= UNKNOWNOBJECT and not RoleOf(name, unit) and UnitIsConnected(unit)
			and (not probedAt[name] or now - probedAt[name] >= PROBE_RETRY) and CanInspect(unit) then
			firstSeen[name] = firstSeen[name] or now
			if now - firstSeen[name] >= PROBE_DELAY then
				probedAt[name] = now
				inspecting = { guid = UnitGUID(unit), name = name, at = now }
				NotifyInspect(unit)
				return
			end
		end
	end
end

local function OnInspectReady(guid)
	if not inspecting or inspecting.guid ~= guid then return end
	local name = inspecting.name
	inspecting = nil
	local unit = UnitOf(name)
	local spec = unit and GetInspectSpecialization(unit)
	if spec and spec > 0 then
		local role = FROM_BLIZZARD_ROLE[GetSpecializationRoleByID(spec) or ""]
		if role then roleByName[name] = role end
		rangedByName[name] = RANGED_SPECS[spec] or nil
	end
	if not (_G.InspectFrame and InspectFrame:IsShown()) then ClearInspectPlayer() end
end

local function Quiet()
	return GetTime() < quietUntil
end

ChatFrame_AddMessageEventFilter("CHAT_MSG_WHISPER", function(_, _, message)
	if Quiet() and IsStrategyAnswer(message) then return true end
end)
ChatFrame_AddMessageEventFilter("CHAT_MSG_WHISPER_INFORM", function(_, _, message)
	if Quiet() and (message:find("^co ") or message:find("^nc ")) then return true end
end)

-- ---------------------------------------------------------------------------
-- Events and slash command
-- ---------------------------------------------------------------------------
local events = CreateFrame("Frame")
events:RegisterEvent("ADDON_LOADED")
events:RegisterEvent("PLAYER_LOGIN")
events:SetScript("OnEvent", function(self, event, arg1, arg2)
	if event == "ADDON_LOADED" and arg1 == "SquidBotsLite" then
		SquidBotsLiteDB = SquidBotsLiteDB or {}
		SquidBotsLiteCharDB = SquidBotsLiteCharDB or { scale = 1 }
		SetLanguage(SquidBotsLiteDB.lang)
	elseif event == "PLAYER_LOGIN" then
		NoteSelf()
		BuildBar()
		BuildOverlays()
		BuildAlts()
		BuildQuests()
		RefreshTexts()
		for _, e in ipairs({ "CHAT_MSG_WHISPER", "CHAT_MSG_SYSTEM", "GROUP_ROSTER_UPDATE", "UNIT_CONNECTION", "PLAYER_LEVEL_UP",
			"PLAYER_SPECIALIZATION_CHANGED", "INSPECT_READY" }) do
			self:RegisterEvent(e)
		end
		local elapsed = 0
		self:SetScript("OnUpdate", function(_, delta)
			elapsed = elapsed + delta
			if elapsed < 0.3 then return end
			elapsed = 0
			RefreshOverlays()
			compact.Refresh()
			RefreshRelease()
			if inspecting and GetTime() - inspecting.at > 5 then inspecting = nil end   -- out of range: no answer
			ProbeRoles()
			TickAlts()
			TickQuests()
		end)
	elseif event == "CHAT_MSG_WHISPER" then
		local sender = ShortName(arg2)
		if arg1 and sender then OnQuestWhisper(arg1, sender, arg2) end
	elseif event == "CHAT_MSG_SYSTEM" then
		if arg1 then OnSystem(arg1) end
	elseif event == "INSPECT_READY" then
		OnInspectReady(arg1)
	elseif event == "GROUP_ROSTER_UPDATE" or event == "UNIT_CONNECTION" then
		if altsFrame:IsShown() then RefreshAlts() end
	elseif event == "PLAYER_LEVEL_UP" or event == "PLAYER_SPECIALIZATION_CHANGED" then
		if event == "PLAYER_LEVEL_UP" or arg1 == "player" then NoteSelf() end
	end
end)

-- Called by Bindings.xml: a key does what a left click on that bar button does, bar shown or not.
function SquidBotsLite_Order(key)
	local button = buttons[key]
	if button then button.onClick(button, "LeftButton") end
end

SLASH_SQUIDBOTSLITE1 = "/sbl"
SLASH_SQUIDBOTSLITE2 = "/squidlite"
SlashCmdList["SQUIDBOTSLITE"] = function(message)
	local command, rest = string.match(message or "", "^(%S*)%s*(.-)$")
	command = string.lower(command or "")
	if command == "lang" then
		SetLanguage(string.lower(rest))
		RefreshTexts()
		Highlight()
		AltsTexts()
		RefreshAlts()
		Print(L.LANG_SET)
	elseif command == "alts" then
		ShowAlts()
	elseif command == "quests" then
		ShowQuests(rest ~= "" and rest or UnitName("target"))
	elseif command == "raidframes" then
		SquidBotsLiteDB.raidFrames = string.lower(rest) ~= "off"
		Print(SquidBotsLiteDB.raidFrames and L.RAIDFRAMES_ON or L.RAIDFRAMES_OFF)
	elseif command == "roles" then
		SquidBotsLiteDB.autoRoles = string.lower(rest) ~= "off"
		Print(SquidBotsLiteDB.autoRoles and L.ROLES_ON or L.ROLES_OFF)
	elseif command == "gm" then
		SquidBotsLiteDB.gm = not SquidBotsLiteDB.gm
		Print(SquidBotsLiteDB.gm and L.GM_ON or L.GM_OFF)
	elseif command == "reset" then
		SquidBotsLiteCharDB.point = nil
		PlaceBar()
		SetBarShown(true)
	elseif command == "show" then
		SetBarShown(true)
	elseif command == "hide" then
		SetBarShown(false)
	elseif command == "scale" or command == "size" then
		local scale = tonumber((string.gsub(rest or "", ",", "."))) -- "1,2" as French players write it
		if rest == "" then
			Print(string.format(L.SCALE_SET, SquidBotsLiteCharDB.scale or 1))
		elseif scale and scale >= SCALE_MIN and scale <= SCALE_MAX then
			SetBarScale(scale)
		else
			Print(L.SCALE_BAD)
		end
	elseif command == "help" or command == "?" then
		Print(L.HELP)
	else
		SetBarShown(not bar:IsShown())
	end
end
