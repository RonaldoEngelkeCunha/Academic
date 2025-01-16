Shader "Custom/DTR-Water"
{
    Properties
    {
        _Color ("Color", Color) = (78,131,169,255) 
        _MainTex ("Albedo (RGB)", 2D) = "white" {}
        _Glossiness ("Smoothness", Range(0,1)) = 0.5
        _Metallic ("Metallic", Range(0,1)) = 0.0
        
         //Use a flow map with CW/CCW rotational flows, where U/V are represented in R/G channels. In addition, incorporate a non-uniform noise throughout the flow map which will server as a non-uniform surface time sampling value
        [NoScaleOffset] _FlowMap ("Flow Map", 2D) = "black" {}

        [NoScaleOffset] _HeightDerivativeMap("Height Derivative Map", 2D) = "black" {}

        //control the speed of the flow
        _FlowSpeed("Flow Speed", Float) = 1

        //scale the sample flow vectors accordingly, create a stronger flow by multiplying sampled flow vector by a scale factor
        _FlowStrength("Flow Strength", Float) = 1

        //offset the starting textural region of the animation (phase). Portal 2 uses a -0.5 offset to ensure the height of the phase is the most undistorted. 
        _FlowOffset("Flow Offset", Float) = 0

        //jump properties controlling offset of UV phase - meaning the animation is impacted by the LCM of the UV phases
        _UJump ("U jump per phase", Range(-0.25, 0.25)) = 0.25 
        _VJump ("V jump per phase", Range(-0.25, 0.25)) = 0.25

        //allows us to scale the texture, increasing animation speed but also generating higher density ripples/waves
        _Tiling("Tiling", Float) = 1

        //Make ripple/wave height proportional (to a certain point) to the speed of the flow (the magnitude of the flow vector)
        _ProportionalHeightScale ("Height Scale Factor", Float) = 0.5

        //water fog color property
        _FogColor("Water Fog Color", Color) = (0, 0, 0, 0)
        
        //control the fog density, which will be incorporated as a general factor for linear interpolation (with the background)
        _FogDensity("Water Fog Density", Range(0,2)) = 0.1

        //control the size of the UV offset vector used to generate the refraction effect 
        _RefractionStrength("Refraction Strength", Range(0,1)) = 0.25
    }
    SubShader
    {
        //must eliminate shadows to make refractions/transparency look good/work with lighting

        //move the shader to the transparent queue (after all opaque objects have been rendered, depth buffer completed)
        //make sure transparency is supported 
        Tags { "RenderType"="Transparent" "Queue"="Transparent" }
        LOD 200
        GrabPass{"_BackgroundTexture"}

        CGPROGRAM
        #pragma surface surf Standard alpha finalcolor:ResetAlpha
        #include "UnityCG.cginc"
        #include "HLSLSupport.cginc"
        #pragma target 3.0

        sampler2D _HeightDerivativeMap,  _BackgroundTexture, _MainTex, _FlowMap, _CameraDepthTexture;
        float _FlowSpeed, _FlowStrength, _FlowOffset,_UJump, _VJump, _Tiling,  _ProportionalHeightScale, _FogDensity, _RefractionStrength;
        float3 _FogColor;
        float4 _CameraDepthTexture_TexelSize;

        struct Input
        {
            float2 uv_MainTex;
            float4 screenPos; //screen space coordinate values
        };

        half _Glossiness;
        half _Metallic;
        fixed4 _Color;


        float2 AlignWithGrabTexel(float2 uv)
        {
            #if UNITY_UV_STARTS_AT_TOP
            if (_CameraDepthTexture_TexelSize.y < 0)
            {
                uv.y = 1 - uv.y;
            }
            #endif
            return (floor(uv * _CameraDepthTexture_TexelSize.zw) + 0.5) * abs(_CameraDepthTexture_TexelSize.xy);
        }

        //Underwater: Controls overall coloration underwater. Takes in screen-space coordinate and tangent-space normal
        float3 Underwater(float3 normal, float4 screenPos)
        {
            //determine the scaled uvOffset vector, proportional to the refraction strength
            float2 uvOffset = normal.xy * _RefractionStrength;

            uvOffset.y *= _CameraDepthTexture_TexelSize.z * abs(_CameraDepthTexture_TexelSize.y);

            float2 uv = AlignWithGrabTexel((screenPos.xy + uvOffset) / screenPos.w);

            //must ensure the texture is not oriented towards the negative extreme, if so, we must take the inverse to an inverted depth 
            #if UNITY_UV_STARTS_AT_TOP
            if (_CameraDepthTexture_TexelSize.y < 0)
            {
                uv.y = 1 - uv.y;
            }
            #endif

            //find the distance from the camera to our water surface
            float CameraSurfaceDepth= UNITY_Z_0_FAR_FROM_CLIPSPACE(screenPos.z);

            //sample the camera depth texture - use linearEyeDepth to linearize this value, giving us the linear depth from camera to background texture
            float CameraBackgroundDepth = LinearEyeDepth(SAMPLE_DEPTH_TEXTURE(_CameraDepthTexture, uv));

            //the true water depth  is found by subtracting these two
            float WaterDepth = CameraBackgroundDepth - CameraSurfaceDepth;

            //ensure we are not offsetting/refracting anything above or on the surface level - this will give us very noticeable artifacts
            if (WaterDepth < 0)
            {
                uv = AlignWithGrabTexel(screenPos.xy / screenPos.w);
                CameraBackgroundDepth = LinearEyeDepth(SAMPLE_DEPTH_TEXTURE(_CameraDepthTexture, uv));
                WaterDepth = CameraBackgroundDepth - CameraSurfaceDepth;
            }

            //sample the background color
            float3 backgroundColor = tex2D(_BackgroundTexture, uv).rgb;

            //determine the fog factor - exponential fog, so the density increases exponentially with linear increase in water depth
            float fogFactor = exp2(-_FogDensity * WaterDepth);

            //linearly interpolate the color between the fog (and its fog factor) and the pre-existing background color
            return lerp(_FogColor, backgroundColor, fogFactor);
        }


        //DistortedFlow: Controls the core UV distortion of the surface, from an appropriate flow map with both rotating flows and non-uniform noise for timing the flows
        float3 DistortedFlow(float2 uv, float time, float2 flowVector, float2 jump, float flowOffset, float tiling, bool flowB)
        {
            //since we want to blend two flows in opposite moments in their phases, we start by creating two flows, one which has its phase shifted by 0.5 and one by 0
            //this removes the "fading" effect since when flow 1 is at 0, flow2 is at 1
            float phaseOffset = flowB ? 0.5 : 0;

            float3 flow;

            //we want fractional time, since we want to reset the animation (we don't want to have the distortion continuously add distortion to the UV since at a point this starts creating extremely noticeable artifacts
            //we also want to incorporate our offset of the phase here
            float progress = frac(time + phaseOffset);

            //we generate our vector in the direction of the texture's flow by subtracting the current uv coordinates by our sampled flow vector. We then limit the contribution of this flow to the UV by ending it at fractional time of 1.
            flow.xy = uv - flowVector * (progress + flowOffset);
            
            //scale the texture
            flow.xy *= tiling;
            
            flow.xy += phaseOffset;
            
            flow.xy += (time - progress) * jump;
            
            flow.z = 1 - abs(1 - 2 * progress);

            return flow;
        }

        float3 UnpackDerivativeHeight(float4 textureData)
        {
            float3 dh = textureData.agb;
            dh.xy = dh.xy * 2 - 1;
            return dh;
        }

        //disable default blending since we are already blending fog/background
        void ResetAlpha(Input IN, SurfaceOutputStandard o, inout fixed4 finalAlpha)
        {
            finalAlpha.a = 1;
        }

        void surf (Input IN, inout SurfaceOutputStandard o)
        {
            o.Metallic = _Metallic;
            o.Smoothness = _Glossiness;

            float3 flow = tex2D(_FlowMap, IN.uv_MainTex).rgb;
            flow.xy = flow.xy * 2 - 1;
            flow *= _FlowStrength;

            //sample noise in a channel, used to non-uniform distort the surface at different time values
            float noise = tex2D(_FlowMap, IN.uv_MainTex).a;
            float timeNoise =(_Time.y * _FlowSpeed) + noise;

            //create jump vector
            float2 jump = float2(_UJump, _VJump);

            //create our two sampled flows
            float3 Flow1 = DistortedFlow(IN.uv_MainTex, timeNoise, flow.xy, jump, _FlowOffset,_Tiling,false);
            float3 Flow2 = DistortedFlow(IN.uv_MainTex, timeNoise, flow.xy, jump, _FlowOffset,_Tiling,true);
            
            float finalHeightScale = flow.z * _ProportionalHeightScale + 0.1;

            float3 Flow1Deriv = UnpackDerivativeHeight(tex2D(_HeightDerivativeMap, Flow1.xy)) * (Flow1.z * finalHeightScale);
            float3 Flow2Deriv = UnpackDerivativeHeight(tex2D(_HeightDerivativeMap, Flow2.xy)) * (Flow2.z * finalHeightScale);

            o.Normal = normalize(float3 (-(Flow1Deriv.xy + Flow2Deriv.xy), 1));

            fixed4 texA = tex2D(_MainTex, Flow1.xy) * Flow1.z;
            fixed4 texB = tex2D(_MainTex, Flow2.xy) * Flow2.z;

            fixed4 finalColor = (texA + texB) * _Color;
            o.Alpha = finalColor.a;
            o.Albedo = finalColor.rgb;

            o.Emission = Underwater(o.Normal, IN.screenPos) * (1 - finalColor.a);
        }
        ENDCG
    }
    
}
