function preferenceCookie()
{
	this.cookie = false;
	this.prefs = false;
	this.load();
};
preferenceCookie.prototype.get = function(reload)
{
	try 
	{
		if (!this.prefs || reload) 
		{
			// setup our default preferences
			this.prefs = 
			{
				// Global Group
				theme: 'palm-default',
				
				// Startup Group
				updateInterval: 'launch',
				lastUpdate: 0, // will be updated every time update is successful
				fixUnknown: true,
				
				// Main Scene Group
				showAvailableTypes: false,
				showTypeApplication: true,
				showTypeTheme: true,
				showTypePatch: true,
				showTypeOther: true,
				
				// List Scene Group
				listSort: 'default',
				secondRow: 'version,maint',
				listInstalled: false,
				onlyShowFree: false,
				searchDesc: false,
				
				// Background Group
				backgroundUpdates: 'disabled',
				autoInstallUpdates: false//,
				
				// Hidden Advanced Group
				//allowServiceUpdates: false
				//allowFlagSkip: false
			};
			
			// uncomment to delete cookie for testing
			//this.cookie.remove();
			var cookieData = this.cookie.get();
			if (cookieData) 
			{
				for (i in cookieData) 
				{
					this.prefs[i] = cookieData[i];
					
					// temporarily fix the old way the second row setting was stored
					// this should be removed after a while.
					if (i == 'secondRow')
					{
						switch (cookieData[i])
						{
							case 'v&i':   this.prefs['secondRow'] = 'version,id';			break;
							case 'v&m':   this.prefs['secondRow'] = 'version,maint';		break;
							case 'v&d':   this.prefs['secondRow'] = 'version,date';			break;
							case 'p&f':   this.prefs['secondRow'] = 'price,feed';			break;
							case 'p&c':   this.prefs['secondRow'] = 'price,country';		break;
							case 'p&l':   this.prefs['secondRow'] = 'price,license';		break;
							case 'p&v&m': this.prefs['secondRow'] = 'price,version,maint';	break;
							case 'p&v&d': this.prefs['secondRow'] = 'price,version,date';	break;
							case 'p&v&f': this.prefs['secondRow'] = 'price,version,feed';	break;
						}
					} 
				}
			}
			else 
			{
				this.put(this.prefs);
			}
		}
		
		return this.prefs;
	} 
	catch (e) 
	{
		Mojo.Log.logException(e, 'preferenceCookie#get');
	}
};
preferenceCookie.prototype.put = function(obj, value)
{
	try
	{
		this.load();
		if (value)
		{
			this.prefs[obj] = value;
			this.cookie.put(this.prefs);
		}
		else
		{
			this.cookie.put(obj);
		}
	} 
	catch (e) 
	{
		Mojo.Log.logException(e, 'preferenceCookie#put');
	}
};
preferenceCookie.prototype.load = function()
{
	try
	{
		if (!this.cookie) 
		{
			this.cookie = new Mojo.Model.Cookie('preferences');
		}
	} 
	catch (e) 
	{
		Mojo.Log.logException(e, 'preferenceCookie#load');
	}
};

// Local Variables:
// tab-width: 4
// End:
