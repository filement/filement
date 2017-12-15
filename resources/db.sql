create database filement DEFAULT CHARACTER SET utf8 DEFAULT COLLATE utf8_general_ci;
use filement;

create table platforms(
	platform_id integer not null auto_increment,
	arch varchar(32) not null,
	device varchar(32) not null,
	os varchar(32) not null,
	format varchar(32) not null,
	primary key(platform_id)
);

create table devices(
	uuid varchar(32) not null,
	client_id integer not null,
	platform_id integer not null,
	version_major integer not null,
	version_minor integer not null,
	revision integer not null,
	name varchar(64) not null,
	registered timestamp not null default current_timestamp,
	secret varbinary(16) not null, -- TODO: is this MySQL-specific?
	public boolean not null default 1,
	primary key(uuid)
);
create view dev as select uuid,client_id,platform_id,concat(version_major,'.',version_minor,'.',revision) as version,name,registered from devices order by registered;

create table versions(
	platform_id integer not null,
	version_major integer not null,
	version_minor integer not null,
	revision integer not null,
	released timestamp not null,
	priority integer not null,
	primary key(platform_id,version_major,version_minor,revision)
);
create view ver as select versions.platform_id,OS,device,arch,format,concat(OS,'.',device,'.',arch,'.',format) as directory,concat(version_major,'.',version_minor,'.',revision) as version,released from versions inner join platforms on versions.platform_id=platforms.platform_id order by OS,released;

create table devices_locations(
	uuid varchar(32) not null,
	host varchar(128) not null;
	port integer not null
);
-- TODO currently port is not used. it should be removed or another one should be added (for https)

create table serials(
	serial varbinary(32) not null, -- TODO: is this MySQL-specific?
	uuid varchar(32) not null,
	primary key(serial)
);

-- TODO: UTF-8

--

insert into platforms(os,device,arch,format) values
	('MacOS','PC','x86','Mach-O'),
	('MacOS','PC','x86_64','Mach-O'),
	('Linux','PC','x86','ELF'),
	('Linux','PC','x86_64','ELF'),
	('FreeBSD','PC','x86','ELF'),
	('FreeBSD','PC','x86_64','ELF'),
	('iOS','iPad','ARM','Mach-O'),
	('iOS','iPhone','ARM','Mach-O'),
	('Android','Tablet','ARM','dalvik'),
	('Android','Phone','ARM','dalvik'),
	('Windows','Dummy','','');
