--
DELETE FROM `smart_scripts` WHERE (`entryorguid` = 16819) AND (`source_type` = 0) AND (`id` IN (3, 4));
INSERT INTO `smart_scripts` (`entryorguid`, `source_type`, `id`, `link`, `event_type`, `event_phase_mask`, `event_chance`, `event_flags`, `event_param1`, `event_param2`, `event_param3`, `event_param4`, `event_param5`, `event_param6`, `action_type`, `action_param1`, `action_param2`, `action_param3`, `action_param4`, `action_param5`, `action_param6`, `target_type`, `target_param1`, `target_param2`, `target_param3`, `target_param4`, `target_x`, `target_y`, `target_z`, `target_o`, `comment`) VALUES
(16819, 0, 3, 4, 52, 0, 100, 0, 1, 16819, 0, 0, 0, 0, 1, 2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 'Force Commander Danath Trollbane - On Text Over Line 1 - Say Line 2'),
(16819, 0, 4, 0, 61, 0, 100, 0, 0, 0, 0, 0, 0, 0, 50, 184640, 7200, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 'Force Commander Danath Trollbane - On Text 1 Over - Summon Gameobject \'Magtheridon\'s Head\'');

UPDATE `gameobject_template` SET `ScriptName` = 'go_matheridons_head' WHERE (`entry` = 184640);
-- https://github.com/cmangos/wotlk-db/commit/631bd8f808d18bb09e4b3ae8ed14c2703429df50
UPDATE `gameobject_template_addon` SET `faction` = 114 WHERE (`entry` = 184640);
