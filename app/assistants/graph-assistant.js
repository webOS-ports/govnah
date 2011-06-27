function GraphAssistant(display)
{
	// setup menu
	this.menuModel =
	{
		visible: true,
		items:
		[
			{
				label: $L("Help"),
				command: 'do-help'
			}
		]
	};
	
	this.display = display;
	
	this.isVisible = true;
};

GraphAssistant.prototype.setup = function()
{
	this.controller.setupWidget(Mojo.Menu.appMenu, { omitDefaultItems: true }, this.menuModel);
	
	this.titleElement = this.controller.get('title');
	this.canvasElement = this.controller.get('graphCanvas');
	
	if (this.display == 'freq')
		this.titleElement.innerHTML = $L('Frequency Graph');
	if (this.display == 'temp')
		this.titleElement.innerHTML = $L('Temperature Graph');
    if (this.display == 'curr')
    	this.titleElement.innerHTML = $L('Current Draw Graph');
	if (this.display == 'load')
		this.titleElement.innerHTML = $L('Load Average Graph');
	if (this.display == 'mem')
		this.titleElement.innerHTML = $L('Memory Usage Graph');
	if (this.display == 'time')
		this.titleElement.innerHTML = $L('Time In State');
	
    this.gestureStartHandler =	this.gestureStartHandler.bindAsEventListener(this);
    this.gestureChangeHandler =	this.gestureChangeHandler.bindAsEventListener(this);
    this.gestureEndHandler =	this.gestureEndHandler.bindAsEventListener(this);
	this.flickHandler =			this.flickHandler.bindAsEventListener(this);
	this.dragStartHandler =		this.dragStartHandler.bindAsEventListener(this);
	this.draggingHandler =		this.draggingHandler.bindAsEventListener(this);
	this.dragEndHandler =		this.dragEndHandler.bindAsEventListener(this);
	
    this.controller.listen(this.canvasElement, 'gesturestart',			this.gestureStartHandler);
    this.controller.listen(this.canvasElement, 'gesturechange',			this.gestureChangeHandler);
    this.controller.listen(this.canvasElement, 'gestureend',			this.gestureEndHandler);
	this.controller.listen(this.canvasElement, Mojo.Event.flick,		this.flickHandler);
	this.controller.listen(this.canvasElement, Mojo.Event.dragStart,	this.dragStartHandler);
	this.controller.listen(this.canvasElement, Mojo.Event.dragging,		this.draggingHandler);
	this.controller.listen(this.canvasElement, Mojo.Event.dragEnd,		this.dragEndHandler);
	
	
	this.visible = this.visible.bindAsEventListener(this);
	this.invisible = this.invisible.bindAsEventListener(this);
	this.controller.listen(this.controller.stageController.document, Mojo.Event.stageActivate,   this.visible);
	this.controller.listen(this.controller.stageController.document, Mojo.Event.stageDeactivate, this.invisible);
	this.backElement = this.controller.get('header');
	this.backTapHandler = this.backTap.bindAsEventListener(this);
	this.controller.listen(this.backElement, Mojo.Event.tap, this.backTapHandler);
};
GraphAssistant.prototype.backTap = function(event)
{
	this.controller.stageController.popScene();
};
GraphAssistant.prototype.gestureStartHandler = function(event)
{
};
GraphAssistant.prototype.gestureChangeHandler = function(event)
{
};
GraphAssistant.prototype.gestureEndHandler = function(event)
{
};

GraphAssistant.prototype.flickHandler = function(event)
{
};
GraphAssistant.prototype.dragStartHandler = function(event)
{
};
GraphAssistant.prototype.draggingHandler = function(event)
{
};
GraphAssistant.prototype.dragEndHandler = function(event)
{
};

GraphAssistant.prototype.orientationChanged = function(orientation)
{
	switch (orientation)
	{
		case 'left':
		case 'right':
			dataHandler.fullGraph.options.renderWidth  = 480;
			dataHandler.fullGraph.options.renderHeight = 320;
			dataHandler.fullGraph.options.yaxis.tics   = 8;
			dataHandler.renderFullGraph();
			break;
			
		case 'up':
		case 'down':
			dataHandler.fullGraph.options.renderWidth  = 320;
			dataHandler.fullGraph.options.renderHeight = 452;
			dataHandler.fullGraph.options.yaxis.tics   = 11;
			dataHandler.renderFullGraph();
			break;
	}
}
GraphAssistant.prototype.activate = function(event)
{
	dataHandler.setGraphAssistant(this);
	
	if (this.controller.stageController.setWindowOrientation)
	{
    	this.controller.stageController.setWindowOrientation("free");
	}
	
	if (this.firstActivate)
	{
		
	}
	else
	{
		
	}
	this.firstActivate = true;
	
	dataHandler.renderFullGraph();
};
GraphAssistant.prototype.deactivate = function(event)
{
};

GraphAssistant.prototype.visible = function(event)
{
	if (!this.isVisible)
	{
		this.isVisible = true;
	}
	
	dataHandler.renderFullGraph();
};
GraphAssistant.prototype.invisible = function(event)
{
	this.isVisible = false;
};

GraphAssistant.prototype.handleCommand = function(event)
{
	if (event.type == Mojo.Event.command)
	{
		switch (event.command)
		{
			case 'do-help':
				this.controller.stageController.pushScene('help');
				break;
		}
	}
};

GraphAssistant.prototype.cleanup = function(event)
{
    this.controller.StopListening(this.canvasElement, 'gesturestart',		this.gestureStartHandler);
    this.controller.StopListening(this.canvasElement, 'gesturechange',		this.gestureChangeHandler);
    this.controller.StopListening(this.canvasElement, 'gestureend',			this.gestureEndHandler);
	this.controller.StopListening(this.canvasElement, Mojo.Event.flick,		this.flickHandler);
	this.controller.StopListening(this.canvasElement, Mojo.Event.dragStart,	this.dragStartHandler);
	this.controller.StopListening(this.canvasElement, Mojo.Event.dragging,	this.draggingHandler);
	this.controller.StopListening(this.canvasElement, Mojo.Event.dragEnd,	this.dragEndHandler);
		
	this.controller.stopListening(this.controller.stageController.document, Mojo.Event.stageActivate,   this.visible);
	this.controller.stopListening(this.controller.stageController.document, Mojo.Event.stageDeactivate, this.invisible);
};

// Local Variables:
// tab-width: 4
// End:
