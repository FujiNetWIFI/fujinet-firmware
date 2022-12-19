function drawPieSlice(ctx, centerX, centerY, radius, startAngle, endAngle, color)
{
    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.moveTo(centerX,centerY);
    ctx.arc(centerX, centerY, radius, startAngle, endAngle);
    ctx.closePath();
    ctx.fill();
}

var Piechart = function(options)
{
    this.options = options;
    this.canvas = options.canvas;
    this.ctx = this.canvas.getContext("2d");
    this.colors = options.colors;
 
    this.draw = function(){
        var total_value = 0;
        var color_index = 0;
        for (var categ in this.options.data){
            var val = this.options.data[categ];
            total_value += val;
        }
 
        var start_angle = 0;
        for (categ in this.options.data){
            val = this.options.data[categ];
            var slice_angle = 2 * Math.PI * val / total_value;
 
            drawPieSlice(
                this.ctx,
                this.canvas.width/2,
                this.canvas.height/2,
                Math.min(this.canvas.width/2,this.canvas.height/2),
                start_angle,
                start_angle+slice_angle,
                this.colors[color_index%this.colors.length]
            );
 
            start_angle += slice_angle;
            color_index++;
        }
 
    }
}

var myData = {
    "used": iSPIFFS_used,
    "free": iSPIFFS_free
};

var myCanvas = document.getElementById("cvsStorage");

myCanvas.width = 300;
myCanvas.height = 300;

var myPiechart = new Piechart(
    {
        canvas:myCanvas,
        data:myData,
        colors:["#2d8edf","#ff9033"]
    }
);

myPiechart.draw();
