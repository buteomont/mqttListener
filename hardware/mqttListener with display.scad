//This is a category header for the customizer 
//It can be more than one line

/* [Size Adjustments] */
//This comment will describe the variable below it in the customizer
width=40; //mm
length=90; 
height=50;
wallThickness=1.6;
frontHeight=25;

displayBezelLength=72.5;
displayBezelWidth=25.9;
displayTotalThickness=13;
displayHoleDiameter=2.3;
displayHoleOffset=2.5; //mm from edge of board
displayHoleOCHoriz=75; //lengthwise
displayHoleOCvert=31; //crosswise
displayBoardLength=80;
displayBoardWidth=36;
displayBoardThickness=1.5;
displayMountHeight=6; //height of standoffs
displayMountDiameter=4;

mp3Width=21.3;
mp3Length=23.3; //includes card sticking out
mp3Thickness=5;
mp3BoardThickness=1.75;

wemosWidth=26;
wemosLength=34.7;
wemosThickness=6.2;
wemosBoardThickness=1.1;
wemosAntennaThickness=1.9;
usbWidth=12;
usbThickness=7;
usbOffset=13.5; //from 5v edge of ESP board

speakerDiameter=27.8;
speakerThickness=3; //edge thickness
soundHoleDiameter=2;
speakerHolderThickness=2;

screwdriverHoleDiameter=5;

//Material used (to adjust for shrinkage)
Material="PLA";//[PLA, ABS, TPU, PETG]

/* [Hidden] */
shrink=[
  ["ABS",0.7],
  ["PLA",0.2],
  ["TPU",0.2],
  ["PETG",0.4]
  ];
temp=search([Material],shrink);
shrinkPCT=shrink[temp[0]][1];
fudge=.02;  //mm, used to keep removal areas non-congruent
shrinkFactor=1+shrinkPCT/100;
$fn=200;
nozzleDiameter=.4;
x=0;
y=1;
z=2;

module go()
  {
  difference()
    {
    solids();
    holes();
    }
  standoffs();
  speaker();
  wemosHolder();
  mp3Holder();
  }

module mp3Holder()
  {
  bottomGap=mp3Width*0.8;
  translate([length-wallThickness-mp3Thickness,width-(mp3Width+1)-wallThickness,0])
    {
    difference()
      {
      cube([mp3Thickness,mp3Width+2,mp3Length+1]);
      translate([mp3Thickness/3,1,0])
        {
        cube([mp3BoardThickness,mp3Width,mp3Length]);
        translate([-wallThickness-1,1,0])
          cube([mp3Thickness+1,mp3Width-2,mp3Length-1]);
        translate([-1-wallThickness,mp3Width/2-bottomGap/2,mp3Length-2+fudge])
          cube([mp3Thickness+1,bottomGap,3]);
        }
      }
    }
  }

module wemosHolder()
  {
  difference()
    {
    union()
      {
      translate([wallThickness,0,2])
        cube([wemosWidth,4,wemosThickness]);
      translate([wemosWidth-5,width-wallThickness*2,2])
        {
        cube([4,2,wemosThickness]);
        translate([-wemosWidth*.7,0,0])
          {
          cube([4,2,wemosThickness]);
          }
        }
      }
//    loadModel();
    translate([wallThickness,wallThickness*2,4])
    cube([wemosWidth,wemosLength,wemosAntennaThickness]);
    }
  }

//module loadModel()
//  {
//  translate([wemosWidth+wallThickness,wallThickness*2,2])
//    rotate([90,0,180])
//      import("/home/david/3D Projects/Cases/ESP D1 Mini/models/WEMOS-D1-mini_stl.STL");
//
//  }
  
module speaker()
  {
  //the holder for the speaker
  translate([length/2-speakerDiameter/2-speakerHolderThickness,width-wallThickness-(speakerHolderThickness+speakerThickness),height/2-speakerDiameter/2])
    speakerHolder();
  }

module solids()
  {
  //the basic outline
  cornerChopCube();
  }

module holes()
  {
  translate([0,0,-fudge])
    cornerChopCube(wallThickness);
  translate([length/2-displayBezelLength/2,width/2-displayBezelWidth/2,frontHeight])
    rotate([45,0,0])
      cube([displayBezelLength,displayBezelWidth,20]);

  //speaker holes in center of back
  translate([length/2,width+fudge/2,height/2])
    {
    rotate([90,0,0])
      {
      for (j=[0:1:3])
        {
        h=speakerDiameter/2-soundHoleDiameter-j*soundHoleDiameter*1.2;
        sp=j+30;
        for (i=[0:sp:350])
          {
          o=h*sin(i);
          a=h*cos(i);
          translate([a,o,0])
          cylinder(h=wallThickness+fudge, d=soundHoleDiameter);
          }
        }
      }
    }

  //screwdriver access to top display mounting screws
  translate([length/2-displayHoleOCHoriz/2,width+wallThickness,frontHeight+displayHoleOCvert*.24])
    rotate([45,0,0])
      screwHoles();

  //hole for USB cable
  translate([usbOffset,width+fudge/2,3])
    {
    rotate([90,0,0])
      {
      hull()
        {
        cylinder(d=usbThickness,h=wallThickness+fudge);
        translate([usbWidth-usbThickness,0,0])
          cylinder(d=usbThickness,h=wallThickness+fudge);
        }
      }
    }
  }

module screwHoles()
  {
  translate([0,0,0])
    {
    cylinder(d=screwdriverHoleDiameter, h=wallThickness*5);
    translate([displayHoleOCHoriz,0,0])
      cylinder(d=screwdriverHoleDiameter, h=wallThickness*5);
    }
  }

module cornerChopCube(shrinkAmount=0)
  {
  difference()
    {
    translate([shrinkAmount,shrinkAmount,0])
      cube([length-shrinkAmount*2,width-shrinkAmount*2,height-shrinkAmount]);
    translate([-fudge/2,shrinkAmount,frontHeight])
      rotate([45,0,0])
        cube([length+fudge,width,height]);
    }
  }
  
module standoffs()
  {
  translate([length/2-displayBoardLength/2-0.5,
            width/2-displayBoardWidth/2+displayMountHeight*cos(45)-displayMountDiameter/2-0.5,
            frontHeight-displayMountHeight+displayMountHeight*sin(45)-displayMountDiameter/2-wallThickness])
    {
    rotate([45,0,0])
      {
      allStandoffs();
      }
    }
  }

module speakerHolder()
  {
  difference() //left side
    {
    cube([speakerThickness+2,speakerHolderThickness+speakerThickness,speakerDiameter]);
    translate([speakerHolderThickness,speakerHolderThickness,-speakerHolderThickness])
      cube([speakerThickness+2,speakerThickness,speakerDiameter]);
    }
  translate([speakerDiameter,0,0]) //right side
    difference() 
      {
      cube([speakerThickness+2,speakerHolderThickness+speakerThickness,speakerDiameter]);
      translate([-speakerHolderThickness,speakerHolderThickness,-speakerHolderThickness])
        cube([speakerThickness+2,speakerThickness,speakerDiameter]);
      }
  }

  //build the standoffs on a horizontal plane and let the caller rotate and place them as a unit
  module allStandoffs()
    {
    translate([0,wallThickness,0])
      {
      translate([displayHoleOffset,displayHoleOffset,0]) //lower left
        {
        standoff(displayMountHeight,displayMountDiameter,displayHoleDiameter-0.25);
        }
      translate([displayBoardLength-displayHoleOffset,displayHoleOffset,0]) //lower right
        {
        standoff(displayMountHeight,displayMountDiameter,displayHoleDiameter-0.25);
        }
      translate([displayHoleOffset,displayBoardWidth-displayHoleOffset,0]) //upper left
        {
        standoff(displayMountHeight,displayMountDiameter,displayHoleDiameter-0.25);
        }
      translate([displayBoardLength-displayHoleOffset,displayBoardWidth-displayHoleOffset,0]) //upper right
        {
        standoff(displayMountHeight,displayMountDiameter,displayHoleDiameter-0.25);
        }
      }
    }

module standoff(height,diameter,holeDiameter=0)
  {
  difference()
    {
    cylinder(h=height, d=diameter);
    cylinder(h=height+wallThickness, d=holeDiameter);
    }
  }

go();
