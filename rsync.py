#!/usr/bin/env python3
import psycopg2.extras
from configparser import ConfigParser, NoOptionError
import os

class Config:
    def __init__(self, configfile='app.conf'):
        self.config = ConfigParser()
        self.config.read(configfile)
        self.configfile = configfile
        self.app = dict()

        if self.config.has_section('database'):
            try:
                self.app['host'] = self.config.get('database', 'host')
            except NoOptionError:
                self.app['host'] = None

        if self.config.has_section('database'):
            try:
                self.app['user'] = self.config.get('database', 'user')
            except NoOptionError:
                self.app['user'] = None

        if self.config.has_section('database'):
            try:
                self.app['pass'] = self.config.get('database', 'pass')
            except NoOptionError:
                self.app['pass'] = None

        if self.config.has_section('database'):
            try:
                self.app['db'] = self.config.get('database', 'db')
            except NoOptionError:
                self.app['database'] = None

queryGetSiteInfo = '''
    SELECT * FROM constellation
'''

if __name__ == "__main__":
    config = Config()

    con = psycopg2.connect(database=config.app['db'],
                           user=config.app['user'],
                           password=config.app['pass'],
                           host=config.app['host'],
                           port="5432")
    db = con.cursor(cursor_factory=psycopg2.extras.DictCursor)

    db.execute(queryGetSiteInfo)
    sites = db.fetchall()
    for site in sites:
        cmd = '/usr/bin/env rsync -avP --delete /tank/u80/www root@{0}:/var'.format(site['ipv4'])
        print(cmd)
        os.system(cmd)

