REVOKE ALL PRIVILEGES ON * . * FROM 'wcloud'@'localhost';

REVOKE ALL PRIVILEGES ON `world` . * FROM 'wcloud'@'localhost';

REVOKE GRANT OPTION ON `world` . * FROM 'wcloud'@'localhost';

REVOKE ALL PRIVILEGES ON `characters` . * FROM 'wcloud'@'localhost';

REVOKE GRANT OPTION ON `characters` . * FROM 'wcloud'@'localhost';

REVOKE ALL PRIVILEGES ON `auth` . * FROM 'wcloud'@'localhost';

REVOKE GRANT OPTION ON `auth` . * FROM 'wcloud'@'localhost';

DROP USER 'wcloud'@'localhost';

DROP DATABASE IF EXISTS `world`;

DROP DATABASE IF EXISTS `characters`;

DROP DATABASE IF EXISTS `auth`;
