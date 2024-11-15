--
-- TK
INSERT INTO zone_difficulty_mythicmode_creatureoverrides (CreatureEntry, HPModifier, HPModifierNormal, Enabled, Comment) VALUES
(20064, 5.0, 5.0, 1, 'Test Thaladred advisor');
-- BWL
UPDATE zone_difficulty_info SET Enabled=1 WHERE MapID=469;
INSERT INTO zone_difficulty_mythicmode_creatureoverrides (CreatureEntry, HPModifier, HPModifierNormal, Enabled, Comment) VALUES(13020, 5, 10, 1, 'Vaelastraz test');
-- BT
UPDATE zone_difficulty_mythicmode_creatureoverrides SET HPModifier=5.0, HPModifierNormal=5.0 WHERE CreatureEntry = 23419;
